# mye_reflect — L1.5 리플렉션·직렬화 프레임워크 (refl::TypeId·TypeInfo·TypeBuilder·
#   TypeRegistry·PropertyPath / ser::IArchive·MemoryStream·JsonArchive·Serialize)
#
# 의존: mye_core만(Json·Base). 04 문서: reflect는 core 위·asset 아래 L1.5 — asset이 소비.
#   (asset::AssetRef 는 전방선언만 사용 — reflect가 asset을 링크하지 않는다.)
add_library(mye_reflect STATIC
    include/mye/refl/TypeId.h
    include/mye/refl/Attribute.h
    include/mye/refl/TypeInfo.h
    include/mye/refl/TypeRegistry.h
    include/mye/refl/TypeBuilder.h
    include/mye/refl/PropertyPath.h
    include/mye/ser/Archive.h
    include/mye/ser/JsonArchive.h
    include/mye/ser/Serialize.h

    src/TypeRegistry.cpp
    src/TypeBuilder.cpp
    src/PropertyPath.cpp
    src/Archive.cpp
    src/JsonArchive.cpp
    src/Serialize.cpp
)

target_include_directories(mye_reflect PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_link_libraries(mye_reflect PUBLIC mye_core)
