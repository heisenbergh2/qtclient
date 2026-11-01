set(STRING_ENCODING_TEST_SOURCES
	${CMAKE_CURRENT_SOURCE_DIR}/string_encoding_test.cpp
)

qtclient_add_gtest(qtclient_string_encoding_tests ${STRING_ENCODING_TEST_SOURCES})
