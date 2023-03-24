serenity_component(
    GetBits
    RECOMMENDED
    TARGETS GetBits
)

compile_gml(GetBitsWindow.gml GetBitsWindowGML.h get_bits_window_gml)

set(SOURCES
    GetBitsWidget.cpp
    main.cpp
    BDecoder.cpp
    BEncoder.cpp
    MetaInfo.cpp
)

set(GENERATED_SOURCES
    GetBitsWindowGML.h
)

serenity_app(GetBits ICON hard-disk)
target_link_libraries(GetBits PRIVATE LibCore LibGfx LibGUI LibDesktop LibFileSystemAccessClient LibMain LibCrypto)
