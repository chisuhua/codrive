
#include <linux/types.h>
#include "opu_priv.h"
#include "amdgpu_ids.h"

static unsigned int pasid_bits = 16;
static bool pasids_allocated; /* = false */

bool opu_set_pasid_limit(unsigned int new_limit)
{
	if (new_limit < 2)
		return false;

	if (new_limit < (1U << pasid_bits)) {
		if (pasids_allocated)
			/* We've already allocated user PASIDs, too late to
			 * change the limit
			 */
			return false;

		while (new_limit < (1U << pasid_bits))
			pasid_bits--;
	}

	return true;
}

unsigned int opu_get_pasid_limit(void)
{
	return 1U << pasid_bits;
}

u32 opu_pasid_alloc(void)
{
	int r = amdgpu_pasid_alloc(pasid_bits);

	if (r > 0) {
		pasids_allocated = true;
		return r;
	}

	return 0;
}

void opu_pasid_free(u32 pasid)
{
	amdgpu_pasid_free(pasid);
}
