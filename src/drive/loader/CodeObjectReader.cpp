
status_t CodeObjetReader::LoadExecutable(
        Executable* exec,
        core::IAgent* agent,
        const char* options,
        hsa_loaded_code_object_t* loaded_code_object)
    {
        if (!exec) {
            return ERROR_INVALID_EXECUTABLE;
        }

        hsa_code_object_t code_object = { reinterpret_cast<uint64_t>(code_object_memory) };
        return exec->LoadCodeObject(
            agent, code_object, options, loaded_code_object);
    }


