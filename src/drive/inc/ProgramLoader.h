#pragma once

#include <map>
#include <vector>

#include "inc/codeobject.h"
#include "inc/pps.h"
#include "inc/pps_loader.h"
#include "inc/pps_loader_context.h"
#include "util/locks.h"
// #include "util/utils.h"

//---------------------------------------------------------------------------//
//    Constants                                                              //
//---------------------------------------------------------------------------//

#define HSA_ARGUMENT_ALIGN_BYTES 16
#define HSA_QUEUE_ALIGN_BYTES 64
#define HSA_PACKET_ALIGN_BYTES 64

///     platform(orignal program) is init by DlaContext,
//      but exexutable program is init by api

/// @brief  ProgramLoader class provides the following functions:
/// - call platform to open and close connection to kernel driver.
/// - load supported extension library (image and finalizer).
/// - load tools library.
/// - call platform to expose supported agents.
/// - maintain loader state.
class ProgramLoader {
public:
    hcs::loader::Loader* loader() { return loader_; }

    hcs::LoaderContext* loader_context() { return &loader_context_; }

    hcs::code::CodeObjectManager* code_manager() { return &code_manager_; }

protected:
    ProgramLoader()
    {
        loader_ = hcs::loader::Loader::Create(&loader_context_);
    };

    ProgramLoader(const ProgramLoader&);

    ProgramLoader& operator=(const ProgramLoader&);

    ~ProgramLoader()
    {
        hcs::loader::Loader::Destroy(loader_);
        loader_ = nullptr;
    }

    /// @brief Load Platform Open connection to kernel driver.
    status_t Load();

    /// @brief Close Platform connection to kernel driver and cleanup resources.
    void Unload();

    // Loader instance.
    hcs::loader::Loader* loader_;

    // Loader context.
    hcs::LoaderContext loader_context_;

    // Code object manager.
    hcs::code::CodeObjectManager code_manager_;

    // Holds reference count to program object.
    std::atomic<uint32_t> ref_count_;
};

