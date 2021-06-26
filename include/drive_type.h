#pragma  once


// * @brief Agent attributes.
typedef enum {
   //* Agent name. The type of this attribute is a NUL-terminated char[64]. The
  HSA_AGENT_INFO_NAME = 0,
  // * Name of vendor. The type of this attribute is a NUL-terminated char[64].
  HSA_AGENT_INFO_VENDOR_NAME = 1,
  // * Agent capability. The type of this attribute is ::hsa_agent_feature_t.
  HSA_AGENT_INFO_FEATURE = 2,
  HSA_AGENT_INFO_MACHINE_MODEL = 3,
  HSA_AGENT_INFO_PROFILE = 4,
  HSA_AGENT_INFO_DEFAULT_FLOAT_ROUNDING_MODE = 5,
  HSA_AGENT_INFO_WAVEFRONT_SIZE = 6,
  HSA_AGENT_INFO_WORKGROUP_MAX_DIM = 7,
  HSA_AGENT_INFO_WORKGROUP_MAX_SIZE = 8,
  HSA_AGENT_INFO_GRID_MAX_DIM = 9,
  HSA_AGENT_INFO_GRID_MAX_SIZE = 10,
  HSA_AGENT_INFO_QUEUE_MIN_SIZE = 13,
  // * Maximum number of packets that a queue created in the agent can
  // * hold. Must be a power of 2 greater than 0. The type of this attribute is uint32_t.
  HSA_AGENT_INFO_QUEUE_MAX_SIZE = 14,
  // * Type of a queue created in the agent. The type of this attribute is * ::queue_type32_t.
  HSA_AGENT_INFO_QUEUE_TYPE = 15,
  HSA_AGENT_INFO_NODE = 16,
  // * Type of hardware device associated with the agent. The type of this * attribute is ::hsa_device_type_t.
  HSA_AGENT_INFO_TYPE = 17,
  HSA_AGENT_INFO_CACHE_SIZE = 18,
  HSA_AGENT_INFO_ISA = 19,
  // * Bit-mask indicating which extensions are supported by the agent. An
  // * extension with an ID of @p i is supported if the bit at position @p i is
  // * set. The type of this attribute is uint8_t[128].
  HSA_AGENT_INFO_EXTENSIONS = 20,
  // * Major version of the HSA runtime specification supported by the
  // * agent. The type of this attribute is uint16_t.
  HSA_AGENT_INFO_VERSION_MAJOR = 21,
  // * Minor version of the HSA runtime specification supported by the
  // * agent. The type of this attribute is uint16_t.
  HSA_AGENT_INFO_VERSION_MINOR = 22,
  HSA_AGENT_INFO_FAST_F16_OPERATION = 24,
  HSA_AGENT_INFO_IS_APU_NODE = 25,
  /** * Chip identifier. The type of this attribute is uint32_t.  */
  HSA_AMD_AGENT_INFO_CHIP_ID = 0xA000,
  /** * Size of a cacheline in bytes. The type of this attribute is uint32_t.  */
  HSA_AMD_AGENT_INFO_CACHELINE_SIZE = 0xA001,
  /** * The number of compute unit available in the agent. The type of this
   * attribute is uint32_t.  */
  HSA_AMD_AGENT_INFO_COMPUTE_UNIT_COUNT = 0xA002,
  /** * The maximum clock frequency of the agent in MHz. The type of this
   * attribute is uint32_t.  */
  HSA_AMD_AGENT_INFO_MAX_CLOCK_FREQUENCY = 0xA003,
  /** * Internal driver node identifier. The type of this attribute is uint32_t.  */
  HSA_AMD_AGENT_INFO_DRIVER_NODE_ID = 0xA004,
  /** * Max number of watch points on memory address ranges to generate exception
   * events when the watched addresses are accessed.  The type of this
   * attribute is uint32_t.  */
  HSA_AMD_AGENT_INFO_MAX_ADDRESS_WATCH_POINTS = 0xA005,
  /** * Agent BDF_ID, named LocationID in thunk. The type of this attribute is
   * uint32_t.  */
  HSA_AMD_AGENT_INFO_BDFID = 0xA006,
  /** * Memory Interface width, the return value type is uint32_t.
   * This attribute is deprecated.  */
  HSA_AMD_AGENT_INFO_MEMORY_WIDTH = 0xA007,
  /** * Max Memory Clock, the return value type is uint32_t.  */
  HSA_AMD_AGENT_INFO_MEMORY_MAX_FREQUENCY = 0xA008,
  /** * Board name of Agent - populated from MarketingName of Kfd Node
   * The value is an Ascii string of 64 chars.  */
  HSA_AMD_AGENT_INFO_PRODUCT_NAME = 0xA009,
  /** * Maximum number of waves possible in a Compute Unit.
   * The type of this attribute is uint32_t.  */
  HSA_AMD_AGENT_INFO_MAX_WAVES_PER_CU = 0xA00A,
  /** * Number of SIMD's per compute unit CU
   * The type of this attribute is uint32_t.  */
  HSA_AMD_AGENT_INFO_NUM_SIMDS_PER_CU = 0xA00B,
  /** * Number of Shader Engines (SE) in Gpu
   * The type of this attribute is uint32_t.  */
  HSA_AMD_AGENT_INFO_NUM_SHADER_ENGINES = 0xA00C,
  /** * Number of Shader Arrays Per Shader Engines in Gpu
   * The type of this attribute is uint32_t.  */ 
  HSA_AMD_AGENT_INFO_NUM_SHADER_ARRAYS_PER_SE = 0xA00D,
  /** * Address of the HDP flush registers.  Use of these registers does not conform to the HSA memory
   * model and should be treated with caution.
   * The type of this attribute is hsa_amd_hdp_flush_t.  */
  HSA_AMD_AGENT_INFO_HDP_FLUSH = 0xA00E,
  /** * PCIe domain for the agent.  Pairs with HSA_AMD_AGENT_INFO_BDFID
   * to give the full physical location of the Agent.
   * The type of this attribute is uint32_t.  */
  HSA_AMD_AGENT_INFO_DOMAIN = 0xA00F,
  /** * Queries for support of cooperative queues.  See ::HSA_QUEUE_TYPE_COOPERATIVE.
   * The type of this attribute is bool.  */
  HSA_AMD_AGENT_INFO_COOPERATIVE_QUEUES = 0xA010,
  /** * Queries UUID of an agent. The value is an Ascii string with a maximum
   * of 21 chars including NUL. The string value consists of two parts: header
   * and body. The header identifies device type (GPU, CPU, DSP) while body
   * encodes UUID as a 16 digit hex string
   *
   * Agents that do not support UUID will return the string "GPU-XX" or
   * "CPU-XX" or "DSP-XX" depending upon their device type ::hsa_device_type_t */
  HSA_AMD_AGENT_INFO_UUID = 0xA011,
  /** * Queries for the ASIC revision of an agent. The value is an integer that
   * increments for each revision. This can be used by user-level software to
   * change how it operates, depending on the hardware version. This allows
   * selective workarounds for hardware errata.
   * The type of this attribute is uint32_t.  */
  HSA_AMD_AGENT_INFO_ASIC_REVISION = 0xA012

} agent_info_t;

