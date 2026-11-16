set(CMAKE_FOLDER Tests)

find_package(NanoTest REQUIRED)

nano_add_executable(MiroTests SOURCES
        JsonTests.cpp
        JsonUnicodeEscapeTests.cpp
        JsonAccessTests.cpp
        UnicodeTests.cpp
        JsonNumberTests.cpp
        JsonNumberReflectionTests.cpp
        JsonNumberLocaleTests.cpp
        JsonObjectTests.cpp
        JsonDefaultsTests.cpp
        JsonCallFormTests.cpp
        JsonOptionsTests.cpp
        XmlTests.cpp
        ReflectorTests.cpp
        PackedArrayTests.cpp
        MissingContainerTests.cpp
        NumberRangeTests.cpp
        TypeIdTests.cpp
        XmlReflectorTests.cpp
        CommandTableTests.cpp
        CommandExportTests.cpp
        MethodInfoTests.cpp
        EventTests.cpp
        ApiReflectorTests.cpp
        CodegenMainTests.cpp
        VirtualReflectorTests.cpp
        SchemaReflectorTests.cpp
        SchemaDialectTests.cpp
        TypeScriptTests.cpp
        CppExportTests.cpp
        CppClientTests.cpp
        EAContainerTests.cpp
        PolymorphicTests.cpp
        TaggedUnionTests.cpp
        UserPrimitiveTests.cpp
        CustomOptionsTests.cpp
        EnumFormatTests.cpp
        RawJsonTests.cpp
        OmittableTests.cpp
        DescriptionTests.cpp
        AttributeTests.cpp
        VisitObjectTests.cpp
        ReflectableConceptTests.cpp
        TypeNameTests.cpp
        PublicHeaders/JsonHeaderTests.cpp
        PublicHeaders/UnicodeHeaderTests.cpp
        PublicHeaders/ReflectHeaderTests.cpp
        PublicHeaders/XmlHeaderTests.cpp
        PublicHeaders/BridgeHeaderTests.cpp
        PublicHeaders/CodegenHeaderTests.cpp
        PublicHeaders/OpenAIHeaderTests.cpp
        OpenAITests.cpp
        TARGETS Miro)

# MiroFormats is an OBJECT library carrying the static-init format
# registrations — linking it splices the format-registry initializers
# directly into the test runner, so buildCodegen<Apis...> tests see
# the same Format set the codegen executables do.
target_link_libraries(MiroTests PRIVATE miro_warnings MiroFormats)

nano_add_executable(MiroRecursionTests SOURCES
        RecursionTests.cpp
        TARGETS Miro)

target_link_libraries(MiroRecursionTests PRIVATE miro_warnings)

# Replaces the global operator new to count allocations, so it gets an
# executable of its own rather than disturbing every other test.
nano_add_executable(MiroAttributeAllocationTests SOURCES
        AttributeAllocationTests.cpp
        TARGETS Miro)

target_link_libraries(MiroAttributeAllocationTests PRIVATE miro_warnings)

add_subdirectory(Benchmark)
