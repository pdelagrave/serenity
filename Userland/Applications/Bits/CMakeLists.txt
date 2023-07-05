serenity_component(
        Bits
        RECOMMENDED
        TARGETS Bits
)

set(SOURCES
        LibBits/Bencode/BDecoder.cpp
        LibBits/Bencode/BEncoder.cpp
        LibBits/Net/Comm.cpp
        LibBits/Net/Connection.cpp
        LibBits/Announcer.cpp
        LibBits/BitField.cpp
        LibBits/BitTorrentMessage.cpp
        LibBits/Checker.cpp
        LibBits/Engine.cpp
        LibBits/Files.cpp
        LibBits/MetaInfo.cpp
        LibBits/PeerContext.cpp
        LibBits/Peer.cpp
        LibBits/PieceHeap.cpp
        LibBits/TorrentDataFileMap.cpp
        LibBits/Torrent.cpp
        Tabs/GeneralTorrentInfoWidget.cpp
        Tabs/PeersTabWidget.cpp
        BitsWidget.cpp
        main.cpp
        )

serenity_app(Bits ICON hard-disk)
target_link_libraries(Bits PRIVATE LibCore LibGfx LibGUI LibDesktop LibFileSystemAccessClient LibMain LibCrypto LibThreading LibProtocol LibHTTP)
