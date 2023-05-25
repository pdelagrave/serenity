serenity_component(
    Bits
    RECOMMENDED
    TARGETS Bits
)

compile_gml(BitsWindow.gml BitsWindowGML.h get_bits_window_gml)

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
        )

set(GENERATED_SOURCES
    BitsWindowGML.h
)

serenity_app(Bits ICON hard-disk)
target_link_libraries(Bits PRIVATE LibCore LibGfx LibGUI LibDesktop LibFileSystemAccessClient LibMain LibCrypto LibThreading LibProtocol LibHTTP)
