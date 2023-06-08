/*
 * Copyright (c) 2023, Pierre Delagrave <pierre.delagrave@gmail.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "PeersTabWidget.h"
#include "Torrent.h"
#include <LibGUI/BoxLayout.h>
#include <LibGUI/Model.h>

namespace Bits {

class PeerListModel final : public GUI::Model {
public:
    explicit PeerListModel(Optional<NonnullRefPtr<Torrent>> torrent)
    {
        set_torrent(torrent);
    }

    enum Column {
        IP,
        Port,
        Progress,
        DownloadSpeed,
        UploadSpeed,
        IsChokedByUs,
        IsChokingUs,
        IsInterestedByUs,
        IsInterestingToUs,
        __Count
    };

    virtual int row_count(GUI::ModelIndex const& = GUI::ModelIndex()) const override
    {
        return m_peers.size();
    }

    virtual int column_count(GUI::ModelIndex const& = GUI::ModelIndex()) const override
    {
        return Column::__Count;
    }

    virtual String column_name(int column) const override
    {
        switch (column) {
        case Column::IP:
            return "IP"_short_string;
        case Column::Port:
            return "Port"_short_string;
        case Column::Progress:
            return "Progress"_string.release_value();
        case Column::DownloadSpeed:
            return "Download Speed"_string.release_value();
        case Column::UploadSpeed:
            return "Upload Speed"_string.release_value();
        case Column::IsChokedByUs:
            return "Choked By Us"_string.release_value();
        case Column::IsChokingUs:
            return "Choking Us"_string.release_value();
        case Column::IsInterestedByUs:
            return "Interested By Us"_string.release_value();
        case Column::IsInterestingToUs:
            return "Interesting To Us"_string.release_value();
        default:
            VERIFY_NOT_REACHED();
        }
    }

    virtual GUI::Variant data(GUI::ModelIndex const& index, GUI::ModelRole role) const override
    {
        if (role == GUI::ModelRole::TextAlignment)
            return Gfx::TextAlignment::CenterLeft;
        if (role == GUI::ModelRole::Display) {
            //            dbgln("index.column(): {} m_peers.size(): {}", index.column(), m_peers.size());
            auto& peer = m_peers.at(index.row());
            switch (index.column()) {
            case Column::IP:
                return peer->address().to_string().release_value_but_fixme_should_propagate_errors();
            case Column::Port:
                return peer->port();
            case Column::Progress:
                return DeprecatedString::formatted("{:.1}%", peer->bitfield().progress());
            case Column::DownloadSpeed:
                return "1"_string.release_value();
            case Column::UploadSpeed:
                return "2"_string.release_value();
            case Column::IsChokedByUs:
                return peer->is_choking_peer();
            case Column::IsChokingUs:
                return peer->is_choking_us();
            case Column::IsInterestedByUs:
                return peer->is_interested_in_us();
            case Column::IsInterestingToUs:
                return peer->is_interested_in_peer();
            }
        }
        return {};
    }

    void update()
    {
        m_peers = m_torrent.map([](auto torrent) { return torrent->peers(); }).value_or({});
        did_update(UpdateFlag::DontInvalidateIndices);
    }

    void set_torrent(Optional<NonnullRefPtr<Torrent>> torrent)
    {
        m_torrent = torrent;
        update();
    }

private:
    Optional<NonnullRefPtr<Torrent>> m_torrent;
    Vector<NonnullRefPtr<Peer>> m_peers;
};

PeersTabWidget::PeersTabWidget(Function<Optional<NonnullRefPtr<Torrent>>()> get_current_torrent)
    : m_get_current_torrent(move(get_current_torrent))
{
    set_layout<GUI::VerticalBoxLayout>();
    m_peers_view = add<GUI::TableView>();
    m_peers_view->set_model(make_ref_counted<PeerListModel>(m_get_current_torrent()));
}

void PeersTabWidget::refresh()
{
    static_cast<PeerListModel*>(m_peers_view->model())->update();
}

void PeersTabWidget::custom_event(Core::CustomEvent& event)
{
    if (event.custom_type() == BitsUiEvents::TorrentSelected) {
        set_torrent(m_get_current_torrent());
    }
}

void PeersTabWidget::set_torrent(Optional<NonnullRefPtr<Torrent>> torrent)
{
    static_cast<PeerListModel*>(m_peers_view->model())->set_torrent(torrent);
}

}