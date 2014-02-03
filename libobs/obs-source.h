/******************************************************************************
    Copyright (C) 2013 by Hugh Bailey <obs.jim@gmail.com>

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
******************************************************************************/

#pragma once

#include "util/c99defs.h"
#include "util/darray.h"
#include "util/dstr.h"
#include "util/threading.h"
#include "media-io/audio-resampler.h"
#include "callback/signal.h"
#include "callback/proc.h"

/*
 * ===========================================
 *   Sources
 * ===========================================
 *
 *   A source is literally a "source" of audio and/or video.
 *
 *   A module with sources needs to export these functions:
 *       + enum_[type]
 *
 *   [type] can be one of the following:
 *       + input
 *       + filter
 *       + transition
 *
 *        input: A source used for directly playing video and/or sound.
 *       filter: A source that is used for filtering other sources, modifying
 *               the audio/video data before it is actually played.
 *   transition: A source used for transitioning between two different sources
 *               on screen.
 *
 *   Each individual source is then exported by it's name.  For example, a
 * source named "mysource" would have the following exports:
 *       + mysource_getname
 *       + mysource_create
 *       + mysource_destroy
 *       + mysource_get_output_flags
 *
 *       [and optionally]
 *       + mysource_properties
 *       + mysource_update
 *       + mysource_activate
 *       + mysource_deactivate
 *       + mysource_video_tick
 *       + mysource_video_render
 *       + mysource_getwidth
 *       + mysource_getheight
 *       + mysource_getparam
 *       + mysource_setparam
 *       + mysource_filtervideo
 *       + mysource_filteraudio
 *
 * ===========================================
 *   Primary Exports
 * ===========================================
 *   bool enum_[type](size_t idx, const char **name);
 *       idx: index of the enumeration.
 *       name: pointer to variable that receives the type of the source
 *       Return value: false when no more available.
 *
 * ===========================================
 *   Source Exports
 * ===========================================
 *   const char *[name]_getname(const char *locale);
 *       Returns the full name of the source type (seen by the user).
 *
 * ---------------------------------------------------------
 *   void *[name]_create(obs_data_t settings, obs_source_t source);
 *       Creates a source.
 *
 *       settings: Settings of the source.
 *       source: pointer to main source
 *       Return value: Internal source pointer, or NULL if failed.
 *
 * ---------------------------------------------------------
 *   void [name]_destroy(void *data);
 *       Destroys the source.
 *
 * ---------------------------------------------------------
 *   uint32_t [name]_get_output_flags(void *data);
 *       Returns a combination of one of the following values:
 *           + SOURCE_VIDEO: source has video
 *           + SOURCE_AUDIO: source has audio
 *           + SOURCE_ASYNC_VIDEO: video is sent asynchronously via RAM
 *           + SOURCE_DEFAULT_EFFECT: source uses default effect
 *           + SOURCE_YUV: source is in YUV color space
 *
 * ===========================================
 *   Optional Source Exports
 * ===========================================
 *   obs_properties_t [name]_properties(const char *locale);
 *       Returns the properties of this particular source type, if any.
 *
 * ---------------------------------------------------------
 *   void [name]_update(void *data, obs_data_t settings);
 *       Called to update the settings of the source.
 *
 * ---------------------------------------------------------
 *   void [name]_video_activate(void *data);
 *       Called when the source is being displayed.
 *
 * ---------------------------------------------------------
 *   void [name]_video_deactivate(void *data);
 *       Called when the source is no longer being displayed.
 *
 * ---------------------------------------------------------
 *   void [name]_video_tick(void *data, float seconds);
 *       Called each video frame with the time taken between the last and
 *       current frame, in seconds.
 *
 * ---------------------------------------------------------
 *   void [name]_video_render(void *data);
 *       Called to render the source.
 *
 * ---------------------------------------------------------
 *   uint32_t [name]_getwidth(void *data);
 *       Returns the width of a source, or -1 for maximum width.  If you render
 *       video, this is required.
 *
 * ---------------------------------------------------------
 *   uint32_t [name]_getheight(void *data);
 *       Returns the height of a source, or -1 for maximum height.  If you
 *       render video, this is required.
 *
 * ---------------------------------------------------------
 *   void [name]_getparam(void *data, const char *param, void *buf,
 *                        size_t buf_size);
 *       Gets a source parameter value.  
 *
 *       param: Name of parameter.
 *       Return value: Value of parameter.
 *
 * ---------------------------------------------------------
 *   void [name]_setparam(void *data, const char *param, const void *buf,
 *                        size_t size);
 *       Sets a source parameter value.
 *
 *       param: Name of parameter.
 *       val: Value of parameter to set.
 *
 * ---------------------------------------------------------
 *   struct source_frame *[name]_filter_video(void *data,
 *                                     const struct source_frame *frame);
 *       Filters audio data.  Used with audio filters.
 *
 *       frame: Video frame data.
 *       returns: New video frame data (or NULL if pending)
 *
 * ---------------------------------------------------------
 *   struct filter_audio [name]_filter_audio(void *data,
 *                                     struct filter_audio *audio);
 *       Filters video data.  Used with async video data.
 *
 *       audio: Audio data.
 *       reutrns New audio data (or NULL if pending)
 */

struct obs_source;

struct source_info {
	const char *id;

	/* ----------------------------------------------------------------- */
	/* required implementations */

	const char *(*getname)(const char *locale);

	void *(*create)(obs_data_t settings, obs_source_t source);
	void (*destroy)(void *data);

	uint32_t (*get_output_flags)(void *data);

	/* ----------------------------------------------------------------- */
	/* optional implementations */

	obs_properties_t (*properties)(const char *locale);

	void (*update)(void *data, obs_data_t settings);

	void (*activate)(void *data);
	void (*deactivate)(void *data);

	void (*video_tick)(void *data, float seconds);
	void (*video_render)(void *data);
	uint32_t (*getwidth)(void *data);
	uint32_t (*getheight)(void *data);

	struct source_frame *(*filter_video)(void *data,
			const struct source_frame *frame);
	struct filtered_audio *(*filter_audio)(void *data,
			struct filtered_audio *audio);
};

struct obs_source {
	volatile int                 refs;

	/* source-specific data */
	char                         *name; /* user-defined name */
	enum obs_source_type         type;
	obs_data_t                   settings;
	void                         *data;
	struct source_info           callbacks;

	signal_handler_t             signals;
	proc_handler_t               procs;

	/* used to indicate that the source has been removed and all
	 * references to it should be released (not exactly how I would prefer
	 * to handle things but it's the best option) */
	bool                         removed;

	/* timing (if video is present, is based upon video) */
	volatile bool                timing_set;
	volatile uint64_t            timing_adjust;
	volatile int                 audio_reset_ref;
	uint64_t                     next_audio_ts_min;
	uint64_t                     last_frame_ts;
	uint64_t                     last_sys_timestamp;

	/* audio */
	bool                         audio_failed;
	struct resample_info         sample_info;
	audio_resampler_t            resampler;
	audio_line_t                 audio_line;
	pthread_mutex_t              audio_mutex;
	struct filtered_audio        audio_data;
	size_t                       audio_storage_size;
	float                        volume;

	/* async video data */
	texture_t                    output_texture;
	DARRAY(struct source_frame*) video_frames;
	pthread_mutex_t              video_mutex;

	/* filters */
	struct obs_source            *filter_parent;
	struct obs_source            *filter_target;
	DARRAY(struct obs_source*)   filters;
	pthread_mutex_t              filter_mutex;
	bool                         rendering_filter;
};

extern bool load_source_info(void *module, const char *module_name,
		const char *source_name, struct source_info *info);

bool obs_source_init_handlers(struct obs_source *source);
extern bool obs_source_init(struct obs_source *source,
		const struct source_info *info);

extern void obs_source_activate(obs_source_t source);
extern void obs_source_deactivate(obs_source_t source);
extern void obs_source_video_tick(obs_source_t source, float seconds);
