serenity_component(
    Bits
    RECOMMENDED
    TARGETS Bits
)

set(SOURCES
    BitsWidget.cpp
    main.cpp
    BDecoder.cpp
    BEncoder.cpp
    MetaInfo.cpp
    Torrent.cpp
    TorrentModel.cpp
    Engine.cpp
        Peer.cpp
        TorrentDataFileMap.cpp
        BitField.cpp
        Data/Comm.cpp
        Data/BitTorrentMessage.cpp
        Data/PeerContext.cpp
        PeersTabWidget.cpp
        )

serenity_app(Bits ICON hard-disk)
target_link_libraries(Bits PRIVATE LibCore LibGfx LibGUI LibDesktop LibFileSystemAccessClient LibMain LibCrypto LibThreading LibProtocol LibHTTP)
