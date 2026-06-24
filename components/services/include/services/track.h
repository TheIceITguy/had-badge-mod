/* GPS breadcrumb recording to timestamped files on the SPIFFS storage partition. */
#ifndef SERVICES_TRACK_H
#define SERVICES_TRACK_H

#include <stdbool.h>
#include "esp_err.h"
#include "util/geo.h"

#define TRACK_DIR "/spiffs"

/* Mount the storage partition and start the recording task. */
esp_err_t track_svc_init(void);

/* Start a new track. Returns false if the clock is not set yet (no GPS time),
 * since the file is named from the start time. */
bool track_start(void);
void track_stop(void);

bool track_is_active(void);
int  track_point_count(void);
const char *track_filename(void);   /* basename of the current/last track */

/* Copy up to `cap` of the most-recent recorded points into `out`, oldest-first,
 * and return the number copied. The points come from a bounded RAM ring (not the
 * CSV file), so this is cheap and safe to call from the UI task. The ring is
 * cleared by track_start and kept after track_stop, so map views can draw the
 * current or last trail. */
int track_get_points(geo_pt_t *out, int cap);

#endif /* SERVICES_TRACK_H */
