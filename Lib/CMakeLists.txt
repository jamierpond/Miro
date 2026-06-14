find_package(EADataStructures REQUIRED)
find_package(ResEmbed REQUIRED)

set(CMAKE_FOLDER Miro)

add_library(Miro STATIC
    Miro/Bridge/BindReflector.cpp
    Miro/Bridge/Bridge.cpp
    Miro/CommandExport/CommandExport.cpp
    Miro/CommandExport/ResolvedTypes.cpp
    Miro/Cpp/Cpp.cpp
    Miro/Cpp/CppClient.cpp
    Miro/Detail/StringUtilities.cpp
    Miro/JSON/Json.cpp
    Miro/JSON/Parser.cpp
    Miro/JSON/Printer.cpp
    Miro/XML/Xml.cpp
    Miro/XML/Parser.cpp
    Miro/XML/Printer.cpp
    Miro/Reflection/Any.cpp
    Miro/Reflection/CommandTable.cpp
    Miro/Reflection/JsonReflector.cpp
    Miro/Reflection/XmlReflector.cpp
    Miro/Reflection/Reflector.cpp
    Miro/Schema/Schema.cpp
    Miro/TypeExport/CodegenMain.cpp
    Miro/TypeExport/Format.cpp
    Miro/TypeTree/TypeTree.cpp
    Miro/TypeScript/TypeScript.cpp)
target_include_directories(Miro PUBLIC ${CMAKE_CURRENT_LIST_DIR})
target_compile_options(Miro PUBLIC $<$<CXX_COMPILER_ID:MSVC>:/Zc:preprocessor>)

# Structural reflection lives entirely in headers (ReflectDispatch.h), so
# the toolchain flags must propagate to consumers too — hence PUBLIC. The
# header self-gates on <experimental/meta>; these flags make that header
# available and enable the `template for` member walk it uses.
if (MIRO_ENABLE_REFLECTION)
    target_compile_features(Miro PUBLIC cxx_std_26)
    target_compile_options(Miro PUBLIC -freflection -fexpansion-statements)
endif ()
target_link_libraries(Miro
    PUBLIC ea_data_structures
    PRIVATE miro_warnings)

res_embed_add(Miro
    FILES Miro/TypeScript/Resources/BridgeRuntime.ts
    NAMESPACE MiroResources
    CATEGORY MiroResources)

if (MIRO_UNITY_BUILD)
    set_target_properties(Miro PROPERTIES UNITY_BUILD ON)
endif ()

# Built-in format registrations (zod / ts / backend / ts-server /
# bridge / jsonschema / cpp / cpp-miro / cpp-client). Split out as its
# own OBJECT library so it can be linked into both the codegen
# executables AND the test runner. Plain static-library placement
# would let the linker drop the registrations (nothing references
# their symbols — the work happens at static init); OBJECT placement
# splices the .o files directly into the consumer's link line so the
# initializers always survive.
#
# Built unconditionally — BuiltinFormats.cpp is pure C++ (no
# filesystem I/O, no host-only deps) and MiroTests links it on every
# platform, including cross-compiled iOS.
add_library(MiroFormats OBJECT
    Miro/TypeExport/BuiltinFormats.cpp)
target_link_libraries(MiroFormats PUBLIC Miro)

# File-writing pieces of the codegen pipeline (writeEmittedFiles in
# CodegenWriter.cpp). Split out as its own OBJECT library so the
# generated stub main() emitted by miro_export(...) gets the file I/O
# spliced directly into its link line. Tests don't link this — they
# drive runFormatsToMemory and inspect the strings in memory.
if (NOT CMAKE_CROSSCOMPILING)
    add_library(MiroCodegenWriter OBJECT
        Miro/TypeExport/CodegenWriter.cpp)
    target_link_libraries(MiroCodegenWriter
        PUBLIC Miro
        PRIVATE ghcFilesystem::ghc_filesystem)
endif ()
