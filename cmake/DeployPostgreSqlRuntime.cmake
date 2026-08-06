# Windows 原生发布辅助：把 libpq 及其同版本加密/字符集依赖部署到目标可执行文件旁。
# 只从 CMake 已选中的 PostgreSQL 库目录解析文件，避免误混用其他安装版本；
# Docker/Linux 使用系统包管理器，不执行本函数。
function(orglink_deploy_postgresql_runtime target_name postgresql_import_library)
    if(NOT WIN32 OR NOT TARGET "${target_name}" OR "${postgresql_import_library}" STREQUAL "")
        return()
    endif()

    get_filename_component(_orglink_pg_library_dir "${postgresql_import_library}" DIRECTORY)
    get_filename_component(_orglink_pg_root "${_orglink_pg_library_dir}" DIRECTORY)
    set(_orglink_pg_search_dirs "${_orglink_pg_root}/bin" "${_orglink_pg_library_dir}")
    set(_orglink_pg_runtime_names
        libpq.dll
        libssl-3-x64.dll libcrypto-3-x64.dll
        libssl-3.dll libcrypto-3.dll
        ssleay32.dll libeay32.dll
        libintl-9.dll libintl-8.dll libiconv-2.dll
        zlib1.dll libwinpthread-1.dll libgcc_s_seh-1.dll
        msvcr120.dll vcruntime140.dll)

    set(_orglink_pg_runtime_files)
    foreach(_orglink_runtime_name IN LISTS _orglink_pg_runtime_names)
        find_file(_orglink_runtime_file_${_orglink_runtime_name}
            NAMES "${_orglink_runtime_name}"
            PATHS ${_orglink_pg_search_dirs}
            NO_DEFAULT_PATH)
        if(_orglink_runtime_file_${_orglink_runtime_name})
            list(APPEND _orglink_pg_runtime_files
                "${_orglink_runtime_file_${_orglink_runtime_name}}")
        endif()
    endforeach()

    list(REMOVE_DUPLICATES _orglink_pg_runtime_files)
    if(NOT _orglink_pg_runtime_files)
        message(FATAL_ERROR "Windows PostgreSQL 运行库未找到，无法发布 ${target_name}。")
    endif()
    add_custom_command(TARGET "${target_name}" POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                ${_orglink_pg_runtime_files} "$<TARGET_FILE_DIR:${target_name}>"
        COMMENT "部署 ${target_name} 的 PostgreSQL libpq 运行库"
        VERBATIM)
endfunction()
