#pragma once

#include "Loader.h"

/// @class CodeObjectReader.
/// @brief Code Object Reader Wrapper.
struct CodeObjectReader {
    static CodeObjectReader* CreateFromMemory(
        const void* code_object,
        size_t size)
    {
        assert(size == 0 || "ERROR_INVALID_ARGUMENT");

        CodeObjectReader* co_reader = new (std::nothrow) CodeObjectReader(
            code_object, size, false);
        // CHECK_ALLOC(wrapper);

        return co_reader;
    }

    static status_t Destroy(CodeObjectReader* co_reader)
    {
        if (!co_reader) {
            return ERROR_INVALID_CODE_OBJECT_READER;
        }

        if (co_reader->comes_from_file) {
            delete[](unsigned char*) co_reader->code_object_memory;
        }
        delete co_reader;

        return SUCCESS;
    }

    /// @brief Default constructor.
    CodeObjectReader(
        const void* _code_object_memory, size_t _code_object_size,
        bool _comes_from_file)
        : code_object_memory(_code_object_memory)
        , code_object_size(_code_object_size)
        , comes_from_file(_comes_from_file)
    {
    }

    /// @brief Default destructor.
    ~CodeObjectReader() { }

    status_t LoadExecutable(
        loader::Executable* exec,
        core::IAgent* agent,
        const char* options,
        hsa_loaded_code_object_t* loaded_code_object);

    const void* code_object_memory;
    const size_t code_object_size;
    const bool comes_from_file;
};

