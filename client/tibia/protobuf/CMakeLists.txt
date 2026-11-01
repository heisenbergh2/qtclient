# Generated CIP appearances/sounds. protobuf + protoc come from vcpkg.
if(NOT TARGET protobuf::libprotobuf)
	find_package(Protobuf CONFIG REQUIRED)
endif()
find_package(Threads)

set(TIBIA_PROTOC_EXECUTABLE)
if(TARGET protobuf::protoc)
	set(TIBIA_PROTOC_EXECUTABLE "$<TARGET_FILE:protobuf::protoc>")
elseif(Protobuf_PROTOC_EXECUTABLE)
	set(TIBIA_PROTOC_EXECUTABLE ${Protobuf_PROTOC_EXECUTABLE})
elseif(PROTOBUF_PROTOC_EXECUTABLE)
	set(TIBIA_PROTOC_EXECUTABLE ${PROTOBUF_PROTOC_EXECUTABLE})
endif()

if(NOT TIBIA_PROTOC_EXECUTABLE)
	message(FATAL_ERROR "Could not find Protobuf Compiler (protoc) from vcpkg")
endif()
message(STATUS "Found Protobuf Compiler: ${TIBIA_PROTOC_EXECUTABLE}")

set(_qtclient_proto_target cip_messages)

include_directories(${PROTOBUF_INCLUDE_DIRS})

set(CIP_PROTO_DIR "${CMAKE_SOURCE_DIR}/client/upstream/proto")
set(ProtoFiles
	"${CIP_PROTO_DIR}/appearances.proto"
	"${CIP_PROTO_DIR}/shared.proto"
	"${CIP_PROTO_DIR}/sounds.proto"
	"${CIP_PROTO_DIR}/sounds-common.proto"
)

if (MSVC)
	if(CMAKE_BUILD_TYPE STREQUAL "Debug")
		string(REPLACE "/Zi" "/Z7" CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG}")
		string(REPLACE "/Zi" "/Z7" CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG}")
	elseif(CMAKE_BUILD_TYPE STREQUAL "Release")
		string(REPLACE "/Zi" "/Z7" CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE}")
		string(REPLACE "/Zi" "/Z7" CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE}")
	elseif(CMAKE_BUILD_TYPE STREQUAL "RelWithDebInfo")
		string(REPLACE "/Zi" "/Z7" CMAKE_CXX_FLAGS_RELWITHDEBINFO "${CMAKE_CXX_FLAGS_RELWITHDEBINFO}")
		string(REPLACE "/Zi" "/Z7" CMAKE_C_FLAGS_RELWITHDEBINFO "${CMAKE_C_FLAGS_RELWITHDEBINFO}")
	endif()

	if (BUILD_STATIC_LIBRARY)
		add_library(${_qtclient_proto_target} STATIC ${ProtoFiles})
		set_property(TARGET ${_qtclient_proto_target} PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
	else()
		add_library(${_qtclient_proto_target} ${ProtoFiles})
		set_property(TARGET ${_qtclient_proto_target} PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
	endif()

	target_compile_options(${_qtclient_proto_target} PRIVATE /FS)
	set_target_properties(${_qtclient_proto_target} PROPERTIES COMPILE_PDB_NAME ${_qtclient_proto_target})
else()
	add_library(${_qtclient_proto_target} ${ProtoFiles})
endif()

add_library(protobuf ALIAS ${_qtclient_proto_target})

target_link_libraries(${_qtclient_proto_target}
	PUBLIC
		protobuf::libprotobuf
)
target_compile_definitions(${_qtclient_proto_target} PUBLIC _DISABLE_VECTOR_ANNOTATION _DISABLE_STRING_ANNOTATION)
target_include_directories(${_qtclient_proto_target} PUBLIC ${CMAKE_CURRENT_BINARY_DIR})

set(PROTOBUF_GENERATE_DEPENDENCIES)
if(TARGET protobuf::protoc)
	list(APPEND PROTOBUF_GENERATE_DEPENDENCIES $<TARGET_FILE:protobuf::protoc>)
elseif(TIBIA_PROTOC_EXECUTABLE)
	list(APPEND PROTOBUF_GENERATE_DEPENDENCIES ${TIBIA_PROTOC_EXECUTABLE})
endif()

if(CMAKE_BASE_NAME STREQUAL "em++")
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -pthread")
endif()

if(NOT COMMAND protobuf_generate)
	message(FATAL_ERROR "protobuf_generate CMake function missing — upgrade vcpkg protobuf or include protobuf-generate.cmake")
endif()

protobuf_generate(
	TARGET ${_qtclient_proto_target}
	LANGUAGE cpp
	IMPORT_DIRS ${CIP_PROTO_DIR}
	DEPENDENCIES ${PROTOBUF_GENERATE_DEPENDENCIES}
)
