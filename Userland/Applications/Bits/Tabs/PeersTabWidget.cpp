/*
 * Copyright (c) 2023, Pierre Delagrave <pierre.delagrave@gmail.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "PeersTabWidget.h"
#include <AK/NumberFormat.h>
#include <LibGUI/BoxLayout.h>
#include <LibGUI/Model.h>

class PeerListModel final : public GUI::Model {
public:
    enum Column {
        Connected,
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

    virtual ErrorOr<String> column_name(int column) const override
    {
        switch (column) {
        case Column::Connected:
            return "Connected"_string.release_value();
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
            auto& peer = m_peers.at(index.row());
            switch (index.column()) {
            case Column::Connected:
                return peer.connected;
            case Column::IP:
                return peer.ip;
            case Column::Port:
                return peer.port;
            case Column::Progress:
                return DeprecatedString::formatted("{:.1}%", peer.progress);
            case Column::DownloadSpeed:
                return DeprecatedString::formatted("{}/s", human_readable_size(peer.download_speed));
            case Column::UploadSpeed:
                return DeprecatedString::formatted("{}/s", human_readable_size(peer.upload_speed));
            case Column::IsChokedByUs:
                return peer.we_choking_it;
            case Column::IsChokingUs:
                return peer.it_choking_us;
            case Column::IsInterestedByUs:
                return peer.it_interested;
            case Column::IsInterestingToUs:
                return peer.we_interested;
            }
        }
        return {};
    }

    void update(Vector<Bits::PeerView> peers)
    {
        m_peers = move(peers);
        did_update(UpdateFlag::DontInvalidateIndices);
    }

private:
    Vector<Bits::PeerView> m_peers;
};

PeersTabWidget::PeersTabWidget()
{
    set_layout<GUI::VerticalBoxLayout>();
    m_peers_table_view = add<GUI::TableView>();
    m_peers_table_view->set_model(make_ref_counted<PeerListModel>());
}

void PeersTabWidget::update(Vector<Bits::PeerView> peers)
{
    static_cast<PeerListModel*>(m_peers_table_view->model())->update(peers);
}
