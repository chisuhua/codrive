status_t hsa_memory_register(void* address, size_t size) {
  if (size == 0 && address != NULL) {
    return ERROR_INVALID_ARGUMENT;
  }

  return SUCCESS;
}

status_t hsa_memory_deregister(void* address, size_t size) {
  return SUCCESS;
}

/*
status_t hsa_memory_allocate(region_t region, size_t size, void** ptr) {

  if (size == 0 || ptr == NULL) {
    return ERROR_INVALID_ARGUMENT;
  }

  const core::IMemoryRegion* mem_region = core::IMemoryRegion::Object(region);

  return core::IRuntime::runtime_singleton_->AllocateMemory(
      mem_region, size, core::IMemoryRegion::AllocateNoFlags, ptr);
}
*/

status_t hsa_agent_get_info(core::IAgent* agent,
                                        agent_info_t attribute,
                                        void* value) {
  // IS_OPEN();
  // IS_BAD_PTR(value);
  // const core::IAgent* agent = core::Agent::Object(agent);
  // IS_VALID(agent);
  return agent->GetInfo(attribute, value);
}

