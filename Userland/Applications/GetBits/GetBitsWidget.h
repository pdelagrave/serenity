/*
 * Copyright (c) 2023, Pierre Delagrave <pierre.delagrave@gmail.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include "LibCore/Socket.h"
#include "LibGUI/Widget.h"
#include <LibProtocol/Request.h>

enum class MessageType : u8 {
    Choke = 0x00,
    Unchoke = 0x01,
    Interested = 0x02,
    NotInterest = 0x03,
    Have = 0x04,
    Bitfield = 0x05,
    Request = 0x06,
    Piece = 0x07,
    Cancel = 0x08,
};

class GetBitsWidget final : public GUI::Widget {
    C_OBJECT(GetBitsWidget)
public:
    virtual ~GetBitsWidget() override = default;
    ErrorOr<void> open_file(String const& filename, NonnullOwnPtr<Core::File>);
    void initialize_menubar(GUI::Window&);

protected:
    void custom_event(Core::CustomEvent& event) override;

private:
    GetBitsWidget();
    OwnPtr<Core::File> m_file;
    ByteBuffer m_local_bitfield;
    ByteBuffer m_remote_bitfield;
    bool m_remote_choked = true;
    u32 m_waiting_for = 0;
    u8 m_local_peer_id_bytes[20];
    Core::TCPSocket* m_socket;
    bool m_gothandshake = false;

    static ErrorOr<String> url_encode_bytes(u8 const*, size_t);
    static ErrorOr<String> hexdump(Bytes);
    RefPtr<Protocol::Request> m_request;
    AllocatingMemoryStream* m_response_stream;
    RefPtr<GUI::Action> m_open_action;
    RefPtr<GUI::Toolbar> m_toolbar;
    RefPtr<Protocol::RequestClient> m_protocol_client;
};