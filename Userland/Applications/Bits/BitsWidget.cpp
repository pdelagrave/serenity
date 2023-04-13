/*
 * Copyright (c) 2023, Pierre Delagrave <pierre.delagrave@gmail.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "BitsWidget.h"
#include "BDecoder.h"
#include "MetaInfo.h"
#include <Applications/Bits/BitsWindowGML.h>
#include <LibFileSystemAccessClient/Client.h>
#include <LibGUI/Action.h>
#include <LibGUI/Toolbar.h>
#include <LibProtocol/Request.h>
#include <LibProtocol/RequestClient.h>
#include <string.h>

namespace Bits {
ErrorOr<String> BitsWidget::url_encode_bytes(u8 const* bytes, size_t length)
{
    StringBuilder builder;
    for (size_t i = 0; i < length; ++i) {
        builder.appendff("%{:02X}", bytes[i]);
    }
    return builder.to_string();
}

ErrorOr<String> BitsWidget::hexdump(Bytes bytes)
{
    StringBuilder builder;
    for (size_t i = 0; i < bytes.size(); ++i) {
        builder.appendff("{:02X}", bytes[i]);
    }
    return builder.to_string();
}

struct BittorrentHandshake {
    u8 pstrlen;
    u8 pstr[19];
    u8 reserved[8];
    u8 info_hash[20];
    u8 peer_id[20];
};

ErrorOr<void> BitsWidget::open_file(String const& filename, NonnullOwnPtr<Core::File> file)
{
    dbgln("Opening file {}", filename);
    m_meta_info = TRY(MetaInfo::create(*file));
    TRY(generate_all_request_messages());
    i64 piece_count = AK::ceil_div(m_meta_info->length(), m_meta_info->piece_length());
    i64 bitfield_size = AK::ceil_div(piece_count, 8L);
    m_local_bitfield = TRY(ByteBuffer::create_zeroed(bitfield_size));
    m_remote_bitfield = TRY(ByteBuffer::create_zeroed(bitfield_size));
    dbgln("piece_count: {}  bitfield_size: {}", piece_count, bitfield_size);
    dbgln("length: {}", m_meta_info->length());
    auto url = URL(m_meta_info->announce());
    auto info_hash = TRY(url_encode_bytes(m_meta_info->info_hash(), 20));
    // fill_with_random(m_local_peer_id_bytes, 20);
    memcpy(&m_local_peer_id_bytes, "\x57\x39\x6b\x5d\x72\xb4\x7f\x3a\x4b\x26\xcf\x84\xbf\x6b\x93\x52\x3f\x14\xf8\xca", 20);
    auto peer_id = TRY(url_encode_bytes(m_local_peer_id_bytes, 20));
    auto port = 27007;

    m_file = TRY(Core::File::open("file.iso"sv, Core::File::OpenMode::Write | Core::File::OpenMode::Truncate));
    //    if ((i64)TRY(m_file->size()) != meta_info.length())
    //        TRY(m_file->truncate(meta_info.length()));

    url.set_query(TRY(String::formatted("info_hash={}&peer_id={}&port={}&compact=0&uploaded=1&downloaded=1&left=2", info_hash, peer_id, port)).to_deprecated_string());

    dbgln("URL: {}", url.to_deprecated_string());

    m_protocol_client = TRY(Protocol::RequestClient::try_create());
    HashMap<DeprecatedString, DeprecatedString, CaseInsensitiveStringTraits> request_headers;
    StringView data;
    Core::ProxyData proxy_data {};
    m_request = m_protocol_client->start_request("GET", url, request_headers, data.bytes(), proxy_data);

    m_request->on_finish = [this, &port, &piece_count](bool success, auto) {
        auto maybe_error = [&]() -> ErrorOr<void> {
            if (!success)
                dbgln("Request failed :(");
            else {
                dbgln("Request succeeded");
                auto res = TRY(BDecoder::parse_bencoded(*m_response_stream)).get<bencoded_dict>();
                m_response_stream->close();
                dbgln("keys {}", res.keys().size());
                if (res.get(TRY("failure reason"_string)).has_value()) {
                    dbgln("Failure:  {}", TRY(String::from_utf8(StringView(res.get(TRY("failure reason"_string)).value().get<ByteBuffer>().bytes()))));
                    return {};
                }
                dbgln("interval: {}", res.get(TRY("interval"_string)).value().get<i64>());
                auto peers = res.get(TRY("peers"_string)).value().get<bencoded_list>();
                dbgln("peer len: {}", peers.size());

                Span<u8> peer_id_bytes;
                String selected_peer_ip;
                u16 selected_peer_port;
                String peer_ip;
                u16 peer_port;
                ByteBuffer bbuf;
                for (auto peer : peers) {
                    auto d = peer.get<bencoded_dict>();
                    bbuf = d.get(TRY("peer id"_string)).value().get<ByteBuffer>();
                    peer_id_bytes = bbuf.bytes();

                    auto pid = TRY(hexdump(peer_id_bytes));
                    peer_ip = TRY(String::from_utf8(StringView(d.get(TRY("ip"_string)).value().get<ByteBuffer>().bytes())));
                    peer_port = d.get(TRY("port"_string)).value().get<i64>();
                    dbgln("peer: {}  ip: {}, port: {}", pid, peer_ip, peer_port);
                    if (peer_port != port) {
                        selected_peer_ip = peer_ip;
                        selected_peer_port = peer_port;
                    }
                }

                m_socket = TRY(Core::TCPSocket::connect(selected_peer_ip.to_deprecated_string(), selected_peer_port)).leak_ptr();
                dbgln("Connected to {}:{}", selected_peer_ip, selected_peer_port);

                m_socket->on_ready_to_read = [&] {
                    //                    dbgln("On ready to read!!");
                    auto maybe_error = [&]() -> ErrorOr<void> {
                        if (!m_socket->is_open() || m_socket->is_eof()) {
                            dbgln("Closing socket");
                            m_socket->close();
                            return {};
                        }

                        if (!m_gothandshake) {
                            dbgln("Not received handshake_message yet");
                            auto pending_bytes = TRY(m_socket->pending_bytes());
                            dbgln("pending_bytes: {}", pending_bytes);
                            if (pending_bytes < sizeof(BittorrentHandshake)) {
                                dbgln("pending_bytes: ({}) but must get at least {} bytes for the handshake_message", pending_bytes, sizeof(BittorrentHandshake));
                                return {};
                            }
                            BittorrentHandshake r_handshake;
                            TRY(m_socket->read_until_filled(Bytes { &r_handshake, sizeof(BittorrentHandshake) }));
                            dbgln("Received handshake_message: Protocol: {}, Reserved: {:08b} {:08b} {:08b} {:08b} {:08b} {:08b} {:08b} {:08b}, info_hash: {:20hex-dump}, peer_id: {:20hex-dump}",
                                r_handshake.pstr,
                                r_handshake.reserved[0],
                                r_handshake.reserved[1],
                                r_handshake.reserved[2],
                                r_handshake.reserved[3],
                                r_handshake.reserved[4],
                                r_handshake.reserved[5],
                                r_handshake.reserved[6],
                                r_handshake.reserved[7],
                                r_handshake.info_hash,
                                r_handshake.peer_id);
                            m_gothandshake = true;

                            auto bitfield_message = AK::ByteBuffer();
                            bitfield_message.append(BigEndian<u32>(m_local_bitfield.size() + 1).bytes());
                            bitfield_message.append((u8)MessageType::Bitfield);
                            bitfield_message.append(m_local_bitfield);
                            dbgln("Sending bitfield_message size: {} ", bitfield_message.size());
                            TRY(m_socket->write_until_depleted(bitfield_message.bytes()));

                            auto unchoke_message = AK::ByteBuffer();
                            unchoke_message.append(BigEndian<u32>(1).bytes());
                            unchoke_message.append((u8)MessageType::Unchoke);
                            dbgln("Sending unchoke_message size: {} ", unchoke_message.size());
                            TRY(m_socket->write_until_depleted(unchoke_message.bytes()));

                            // request: <len=0013><id=6><index><begin><length>
                            //                            auto request_message = AK::ByteBuffer();
                            //                            request_message.append(BigEndian<u32>(13).bytes());
                            //                            request_message.append((u8)MessageType::Request);
                            //                            request_message.append(BigEndian<u32>(0).bytes());
                            //                            request_message.append(BigEndian<u32>(0).bytes());
                            //                            auto len = min(16 * KiB, m_meta_info->length());
                            //                            request_message.append(BigEndian<u32>(len).bytes());
                            //                            dbgln("Sending request_message index:{} begin:{} length:{}", 0, 0, len);
                            //                            TRY(m_socket->write_until_depleted(request_message.bytes()));
                            TRY(send_request_messages(24832));
                        }
                        TRY(m_socket->discard(TRY(m_socket->pending_bytes())));
                        TRY(send_request_messages(24832));
                        return {};
                        auto pending_bytes = TRY(m_socket->pending_bytes());
                        //                        dbgln("pending_bytes: {}", pending_bytes);
                        if (pending_bytes < 4) {
                            dbgln("pending_bytes ({}) is < 4", pending_bytes);
                            return {};
                        }

                        if (m_waiting_for == 0) {
                            // read 4 bytes into m_waiting_for
                            m_waiting_for = TRY(m_socket->read_value<BigEndian<u32>>());
                            if (m_waiting_for == 0) {
                                dbgln("Received keepalive");
                                return {};
                            }
                            dbgln("m_waiting_for: {}", m_waiting_for);
                        }
                        pending_bytes = TRY(m_socket->pending_bytes());
                        if (pending_bytes < m_waiting_for) {
                            //                            dbgln("Waiting for {} bytes but only {} are pending in the socket", m_waiting_for, pending_bytes);
                            return {};
                        }

                        auto message_id = TRY(m_socket->read_value<MessageType>());
                        ByteBuffer buffer = TRY(ByteBuffer::create_uninitialized(m_waiting_for - 1));
                        TRY(m_socket->read_until_filled(buffer));
                        m_waiting_for = 0;
                        //                        dbgln("Parsing message type {:02X} of {} bytes: {:s}", (u8)message_id, buffer.size(), TRY(hexdump(buffer)));
                        dbgln("Parsing message type {:02X} of {} bytes", (u8)message_id, buffer.size());

                        switch (message_id) {
                        case MessageType::Choke:
                            dbgln("Got message type Choke");
                            m_remote_choked = true;
                            break;
                        case MessageType::Unchoke:
                            dbgln("Got message type Unchoke");
                            m_remote_choked = false;
                            break;
                        case MessageType::Interested:
                            dbgln("Got message type Interested, unsupported");
                            break;
                        case MessageType::NotInterest:
                            dbgln("Got message type NotInterest, unsupported");
                            break;
                        case MessageType::Have:
                            dbgln("Got message type Have, unsupported");
                            break;
                        case MessageType::Bitfield:
                            dbgln("Got message type Bitfield");
                            if (buffer.size() != m_remote_bitfield.size()) {
                                return Error::from_string_view(TRY(String::formatted("Bitfield sent by peer is {} and was expected to be {}", buffer.size(), m_remote_bitfield.size())));
                            }
                            m_remote_bitfield.overwrite(0, buffer.data(), buffer.size());
                            break;
                        case MessageType::Request:
                            dbgln("Got message type Request, unsupported");
                            break;
                        case MessageType::Piece: {
                            //<len=0009+X><id=7><index><begin><block>
                            auto block_size = buffer.size() - 8;
                            auto s = FixedMemoryStream(buffer.bytes());
                            auto index = TRY(s.read_value<BigEndian<u32>>());
                            auto begin = TRY(s.read_value<BigEndian<u32>>());
                            dbgln("Received piece index {} begin {} blocksize {}", index, begin, block_size);
                            // TRY(m_file->write_until_depleted(TRY(buffer.slice(8, block_size))));
                            //                            u32 next_begin = begin + block_size;
                            //                            dbgln("next_begin: {}", next_begin);
                            //                            dbgln("last_piece_length: {}", m_meta_info->last_piece_length());
                            //                            dbgln("piece_length: {}", m_meta_info->piece_length());
                            //                            dbgln("piece_count: {}", piece_count);
                            //                            i64 piece_length = index == piece_count - 1 ? m_meta_info->last_piece_length() : m_meta_info->piece_length();
                            //                            dbgln("selected piece_length: {}", piece_length);
                            //                            i64 aa = piece_length - next_begin;
                            //                            dbgln("aa: {}", aa);
                            //                            u64 next_request_length;
                            //                            if (aa == 0) {
                            //                                index = index + 1;
                            //                                if (index == piece_count) {
                            //                                    dbgln("Done!");
                            //                                    m_file->close();
                            //                                    return {};
                            //                                }
                            //                                next_begin = 0;
                            //                                if (index == piece_count) {
                            //                                    next_request_length = (16 * KiB) % m_meta_info->last_piece_length();
                            //                                } else {
                            //                                    next_request_length = (16 * KiB) % piece_length;
                            //                                }
                            //                            } else {
                            //                                next_request_length = AK::min(16 * KiB, aa);
                            //                            }
                            //
                            //                            // request: <len=0013><id=6><index><begin><length>
                            //                            auto request_message = AK::ByteBuffer();
                            //                            request_message.append(BigEndian<u32>(13).bytes());
                            //                            request_message.append((u8)MessageType::Request);
                            //                            request_message.append(BigEndian<u32>(index).bytes());
                            //                            request_message.append(BigEndian<u32>(next_begin).bytes());
                            //                            request_message.append(BigEndian<u32>(next_request_length).bytes());
                            //                            dbgln("Sending request_message index:{} begin:{} length:{}", index, next_begin, next_request_length);
                            //                            TRY(m_socket->write_until_depleted(request_message.bytes()));

                            break;
                        }
                        case MessageType::Cancel:
                            dbgln("Got message type Cancel, unsupported");
                            break;
                        default:
                            dbgln("Got unknown message type: {:02X}", (u8)message_id);
                            break;
                        }
                        return {};
                    }.operator()();
                    if (maybe_error.is_error()) {
                        dbgln("Error reading from socket: {}", maybe_error.error().string_literal());
                    }
                };

                auto handshake = AK::ByteBuffer();
                handshake.append(19);
                handshake.append("BitTorrent protocol"sv.bytes());
                u8 flags[8] { 0, 0, 0, 0, 0, 0, 0, 0 };
                //                flags[5] = flags[5] | 0x10;
                handshake.append(flags, 8);
                handshake.append(m_meta_info->info_hash(), 20);
                handshake.append(m_local_peer_id_bytes, 20);
                dbgln("Sending handshake {} bytes: {}", handshake.size(), TRY(url_encode_bytes(handshake.data(), handshake.size())).replace("%"sv, ""sv, ReplaceMode::All));
                TRY(m_socket->write_until_depleted(handshake.bytes()));
            }
            return {};
        }.operator()();
        if (maybe_error.is_error()) {
            dbgln("Error doing peer connection stuff: {}", maybe_error.error().string_literal());
        }
    };

    m_request->stream_into(*m_response_stream);

    return {};
}

ErrorOr<void> BitsWidget::send_request_messages(u64)
{
    //    for (u64 i = 0; i < total; i++) {
    //        TRY(m_socket->write_until_depleted(m_all_request_messages[i].bytes()));
    //    }
    size_t send_size = AK::min(20, m_all_requests.size() - m_msg_reqs_offset);
    if (send_size == 0)
        return {};
    dbgln("Sending {} bytes of request messages ({}/{})", send_size, m_msg_reqs_offset, m_all_requests.size());
    TRY(m_socket->write_until_depleted(Bytes { m_all_requests.data() + m_msg_reqs_offset, send_size }));
    m_msg_reqs_offset += send_size;
    return {};
}

ErrorOr<void> BitsWidget::generate_all_request_messages()
{
    // request: <len=0013><id=6><index><begin><length>
    u64 request_count = AK::ceil_div(m_meta_info->length(), BlockLength);
    i64 piece_count = AK::ceil_div(m_meta_info->length(), m_meta_info->piece_length());
    u64 message_per_piece = request_count / piece_count;
    dbgln("Request count: {}, piece_count: {} message_per_piece: {}", request_count, piece_count, message_per_piece);

    for (u64 i = 0; i < request_count; i++) {
        u64 current_piece = (i * BlockLength) / m_meta_info->piece_length();

        auto request_message = ByteBuffer();
        request_message.append(BigEndian<u32>(13).bytes());
        request_message.append((u8)MessageType::Request);
        request_message.append(BigEndian<u32>(current_piece).bytes());
        request_message.append(BigEndian<u32>((i % message_per_piece) * BlockLength).bytes());
        request_message.append(BigEndian<u32>(BlockLength).bytes());
        m_all_request_messages.append(request_message);
        m_all_requests.append(request_message);
    }
    return {};
}

void BitsWidget::initialize_menubar(GUI::Window& window)
{
    auto& file_menu = window.add_menu("&File");
    file_menu.add_action(*m_open_action);
}

BitsWidget::BitsWidget()
{
    load_from_gml(get_bits_window_gml).release_value_but_fixme_should_propagate_errors();

    m_toolbar = *find_descendant_of_type_named<GUI::Toolbar>("toolbar");

    m_open_action = GUI::CommonActions::make_open_action([this](auto&) {
        auto response = FileSystemAccessClient::Client::the().open_file(window(), {}, Core::StandardPaths::home_directory(), Core::File::OpenMode::Read);
        if (response.is_error())
            return;

        open_file(response.value().filename(), response.value().release_stream()).release_error();
    });

    m_toolbar->add_action(*m_open_action);
    m_response_stream = new AllocatingMemoryStream();
}

}