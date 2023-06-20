serenity_component(
    Bits
    RECOMMENDED
    TARGETS Bits
)

set(SOURCES
    LibBits/Bencode/BDecoder.cpp
    LibBits/Bencode/BEncoder.cpp
    LibBits/Net/BitTorrentMessage.cpp
    LibBits/Net/Comm.cpp
    LibBits/Net/PeerContext.cpp
    LibBits/Net/PieceHeap.cpp
    LibBits/Net/TorrentContext.cpp
    LibBits/BitField.cpp
    LibBits/Engine.cpp
    LibBits/MetaInfo.cpp
    LibBits/TorrentDataFileMap.cpp
    LibBits/Torrent.cpp
    BitsWidget.cpp
    PeersTabWidget.cpp
    main.cpp
    )

serenity_app(Bits ICON hard-disk)
target_link_libraries(Bits PRIVATE LibCore LibGfx LibGUI LibDesktop LibFileSystemAccessClient LibMain LibCrypto LibThreading LibProtocol LibHTTP)
