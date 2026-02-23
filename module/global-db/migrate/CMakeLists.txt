cmake_minimum_required(VERSION 3.10)
project(dap_global_db_migrate C)

# Migration tool sources
# MDBX migration reads files directly (no libmdbx dependency)
set(MIGRATE_SRC
    dap_global_db_migrate.c
    dap_global_db_migrate_mdbx.c
    dap_global_db_migrate_sql.c
)

# Create migration library
add_library(${PROJECT_NAME} STATIC ${MIGRATE_SRC})

target_include_directories(${PROJECT_NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../include)

target_link_libraries(${PROJECT_NAME} dap_global_db)

message("[+] GlobalDB migrate: MDBX file parser (no libmdbx dependency)")
message("[+] GlobalDB migrate: SQL dump parser")

# Migration CLI tool
add_executable(dap_gdb_migrate_tool migrate_tool.c)
# dap_global_db_migrate → dap_global_db → dap_crypto (dap_sign_get_size), etc.
# Use the final shared library to resolve all transitive dependencies
target_link_libraries(dap_gdb_migrate_tool ${PROJECT_NAME} dap_sdk)
