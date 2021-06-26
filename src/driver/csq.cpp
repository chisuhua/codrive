#include "utils/lang/error.h"
#include "inc/unpinned_copy_engine.h"
#include "inc/csq.h"
#include "inc/csq_queue.h"

// Environment variables:
// int HCC_PRINT_ENV=0;
int HCC_SIGNAL_POOL_SIZE = 512;
int HCC_UNPINNED_COPY_MODE = UnpinnedCopyEngine::UseStaging;
int HCC_CHECK_COPY = 0;

// Copy thresholds, in KB.  These are used for "choose-best" copy mode.
long int HCC_H2D_STAGING_THRESHOLD = 64;
long int HCC_H2D_PININPLACE_THRESHOLD = 4096;
long int HCC_D2H_PININPLACE_THRESHOLD = 1024;

// Staging buffer size in KB for unpinned copy engines
int HCC_STAGING_BUFFER_SIZE = 4 * 1024;

// Default GPU device
unsigned int HCC_DEFAULT_GPU = 0;

unsigned int HCC_ENABLE_PRINTF = 0;

// Chicken bits:
int HCC_SERIALIZE_KERNEL = 0;
int HCC_SERIALIZE_COPY = 0;
int HCC_FORCE_COMPLETION_FUTURE = 0;
int HCC_FORCE_CROSS_QUEUE_FLUSH = 0;

int HCC_OPT_FLUSH = 1;

unsigned HCC_DB = 0;
unsigned HCC_DB_SYMBOL_FORMAT = 0x10;

unsigned int HCC_MAX_QUEUES = 20;

int HCC_PROFILE = 0;

#define HCC_PROFILE_VERBOSE_BASIC (1 << 0)	 // 0x1
#define HCC_PROFILE_VERBOSE_TIMESTAMP (1 << 1) // 0x2
#define HCC_PROFILE_VERBOSE_OPSEQNUM (1 << 2)  // 0x4
#define HCC_PROFILE_VERBOSE_TID (1 << 3)	   // 0x8
#define HCC_PROFILE_VERBOSE_BARRIER (1 << 4)   // 0x10
int HCC_PROFILE_VERBOSE = 0x1F;

char *HCC_PROFILE_FILE = nullptr;

namespace csq
{

const char *getHCCRuntimeStatusMessage(const HCCRuntimeStatus status)
{
	const char *message = nullptr;
	switch (status)
	{
	//HCCRT_CASE_STATUS_STRING(HCCRT_STATUS_SUCCESS,"Success");
	case HCCRT_STATUS_SUCCESS:
		message = "Success";
		break;
	case HCCRT_STATUS_ERROR:
		message = "Generic error";
		break;
	case HCCRT_STATUS_ERROR_COMMAND_QUEUE_OVERFLOW:
		message = "Command queue overflow";
		break;
	default:
		message = "Unknown error code";
		break;
	};
	return message;
}

void checkHCCRuntimeStatus(const HCCRuntimeStatus status, const unsigned int line, queue_t *q)
{
	if (status != HCCRT_STATUS_SUCCESS)
	{
		//fprintf(stderr, "### HCC runtime error: %s at %s line:%d\n", getHCCRuntimeStatusMessage(status), __FILENAME__, line);
		std::string m("HCC Runtime Error - ");
		m += getHCCRuntimeStatusMessage(status);
		throw utils::Error(m.c_str());
		//if (q != nullptr)
		//  assert(SUCCESS == hsa_queue_destroy(q));
		//assert(SUCCESS == hsa_shut_down());
		//exit(-1);
	}
}

void AsyncOp::setSeqNumFromQueue() { seqNum = queue->assign_op_seq_num(); };

} // namespace csq
