serenity_component(
    Bits
    RECOMMENDED
    TARGETS Bits
)

set(SOURCES
    BitsWidget.cpp
    main.cpp
        LibBits/Bencode/BDecoder.cpp
        LibBits/Bencode/BEncoder.cpp
        LibBits/MetaInfo.cpp
        LibBits/Torrent.cpp
    TorrentModel.cpp
        LibBits/Engine.cpp
        LibBits/TorrentDataFileMap.cpp
        LibBits/BitField.cpp
        LibBits/Net/Comm.cpp
        LibBits/Net/BitTorrentMessage.cpp
        LibBits/Net/PeerContext.cpp
        PeersTabWidget.cpp
        LibBits/Net/TorrentContext.cpp
        LibBits/Net/PieceHeap.cpp
        )

serenity_app(Bits ICON hard-disk)
target_link_libraries(Bits PRIVATE LibCore LibGfx LibGUI LibDesktop LibFileSystemAccessClient LibMain LibCrypto LibThreading LibProtocol LibHTTP)
