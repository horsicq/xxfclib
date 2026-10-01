add_executable(xxfc_unpack main.c xxfc_readers.c xxfc_readers.h)

add_executable(xxfc_dump_extensions EXCLUDE_FROM_ALL dump_extensions.c xxfc_readers.c)
target_link_libraries(xxfc_dump_extensions PRIVATE xxfclib::xxfclib)
target_compile_features(xxfc_dump_extensions PRIVATE c_std_11)
target_link_libraries(xxfc_unpack PRIVATE xxfclib::xxfclib)
target_compile_features(xxfc_unpack PRIVATE c_std_11)

if(MSVC)
    target_compile_options(xxfc_unpack PRIVATE /W4 /utf-8)
    target_compile_definitions(xxfc_unpack PRIVATE _CRT_SECURE_NO_WARNINGS)
else()
    target_compile_options(xxfc_unpack PRIVATE -Wall -Wextra -Wpedantic)
endif()
