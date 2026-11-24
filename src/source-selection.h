#pragma once

#include <obs.h>
#include <string.h>

#define HS_SOURCE_SELECTION_PREFIX "hs-source:v1:"

static inline bool hs_source_selection_explicit(const char *selection)
{
	return selection && strncmp(selection, HS_SOURCE_SELECTION_PREFIX, sizeof(HS_SOURCE_SELECTION_PREFIX) - 1) == 0;
}

/* New selections are source identities, not scene-item identities. Never
 * redirect an explicit missing UUID to another source with the old name. */
static inline obs_source_t *hs_source_selection_resolve(const char *selection, const char *legacy_uuid)
{
	if (!selection || !*selection)
		return NULL;

	obs_source_t *source = NULL;
	if (hs_source_selection_explicit(selection)) {
		const char *uuid = selection + sizeof(HS_SOURCE_SELECTION_PREFIX) - 1;
		if (*uuid)
			source = obs_get_source_by_uuid(uuid);
	} else {
		if (legacy_uuid && *legacy_uuid)
			source = obs_get_source_by_uuid(legacy_uuid);
		if (!source)
			source = obs_get_source_by_name(selection);
	}
	if (source && obs_source_removed(source)) {
		obs_source_release(source);
		source = NULL;
	}
	return source;
}
