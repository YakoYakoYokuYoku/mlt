/*
 * mlt_consumer.c -- abstraction for all consumer services
 * Copyright (C) 2003-2004 Ushodaya Enterprises Limited
 * Author: Charles Yates <charles.yates@pandora.be>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include "config.h"
#include "mlt_consumer.h"
#include "mlt_factory.h"
#include "mlt_producer.h"
#include "mlt_frame.h"
#include "mlt_profile.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/time.h>

static void mlt_consumer_frame_render( mlt_listener listener, mlt_properties owner, mlt_service this, void **args );
static void mlt_consumer_frame_show( mlt_listener listener, mlt_properties owner, mlt_service this, void **args );
static void mlt_consumer_property_changed( mlt_service owner, mlt_consumer this, char *name );
static void apply_profile_properties( mlt_profile profile, mlt_properties properties );

static mlt_event g_event_listener = NULL;

/** Public final methods
*/

int mlt_consumer_init( mlt_consumer this, void *child )
{
	int error = 0;
	memset( this, 0, sizeof( struct mlt_consumer_s ) );
	this->child = child;
	error = mlt_service_init( &this->parent, this );
	if ( error == 0 )
	{
		// Get the properties from the service
		mlt_properties properties = MLT_SERVICE_PROPERTIES( &this->parent );
	
		// Apply the profile to properties for legacy integration
		apply_profile_properties( mlt_profile_get(), properties );

		// Default rescaler for all consumers
		mlt_properties_set( properties, "rescale", "bilinear" );

		// Default read ahead buffer size
		mlt_properties_set_int( properties, "buffer", 25 );

		// Default audio frequency and channels
		mlt_properties_set_int( properties, "frequency", 48000 );
		mlt_properties_set_int( properties, "channels", 2 );

		// Default of all consumers is real time
		mlt_properties_set_int( properties, "real_time", 1 );

		// Default to environment test card
		mlt_properties_set( properties, "test_card", mlt_environment( "MLT_TEST_CARD" ) );

		// Hmm - default all consumers to yuv422 :-/
		this->format = mlt_image_yuv422;

		mlt_events_register( properties, "consumer-frame-show", ( mlt_transmitter )mlt_consumer_frame_show );
		mlt_events_register( properties, "consumer-frame-render", ( mlt_transmitter )mlt_consumer_frame_render );
		mlt_events_register( properties, "consumer-stopped", NULL );

		// Register a property-changed listener to handle the profile property -
		// subsequent properties can override the profile
		g_event_listener = mlt_events_listen( properties, this, "property-changed", ( mlt_listener )mlt_consumer_property_changed );

		// Create the push mutex and condition
		pthread_mutex_init( &this->put_mutex, NULL );
		pthread_cond_init( &this->put_cond, NULL );

	}
	return error;
}

static void apply_profile_properties( mlt_profile profile, mlt_properties properties )
{
	mlt_event_block( g_event_listener );
	mlt_properties_set_double( properties, "fps", mlt_profile_fps( profile ) );
	mlt_properties_set_int( properties, "frame_rate_num", profile->frame_rate_num );
	mlt_properties_set_int( properties, "frame_rate_den", profile->frame_rate_den );
	mlt_properties_set_int( properties, "width", profile->width );
	mlt_properties_set_int( properties, "height", profile->height );
	mlt_properties_set_int( properties, "progressive", profile->progressive );
	mlt_properties_set_double( properties, "aspect_ratio", mlt_profile_sar( profile )  );
	mlt_properties_set_int( properties, "sample_aspect_num", profile->sample_aspect_num );
	mlt_properties_set_int( properties, "sample_aspect_den", profile->sample_aspect_den );
	mlt_properties_set_double( properties, "display_ratio", mlt_profile_dar( profile )  );
	mlt_properties_set_int( properties, "display_aspect_num", profile->display_aspect_num );
	mlt_properties_set_int( properties, "display_aspect_num", profile->display_aspect_num );
	mlt_event_unblock( g_event_listener );
}

static void mlt_consumer_property_changed( mlt_service owner, mlt_consumer this, char *name )
{
	if ( !strcmp( name, "profile" ) )
	{
		// Get the properies
		mlt_properties properties = MLT_CONSUMER_PROPERTIES( this );

		// Locate the profile
		mlt_profile_select( mlt_properties_get( properties, "profile" ) );

		// Apply to properties
		apply_profile_properties( mlt_profile_get(), properties );
	}
	else if ( !strcmp( name, "frame_rate_num" ) )
	{
		mlt_properties properties = MLT_CONSUMER_PROPERTIES( this );
		mlt_profile_get()->frame_rate_num = mlt_properties_get_int( properties, "frame_rate_num" );
		mlt_properties_set_double( properties, "fps", mlt_profile_fps( NULL ) );
	}
	else if ( !strcmp( name, "frame_rate_den" ) )
	{
		mlt_properties properties = MLT_CONSUMER_PROPERTIES( this );
		mlt_profile_get()->frame_rate_den = mlt_properties_get_int( properties, "frame_rate_den" );
		mlt_properties_set_double( properties, "fps", mlt_profile_fps( NULL ) );
	}
	else if ( !strcmp( name, "width" ) )
	{
		mlt_properties properties = MLT_CONSUMER_PROPERTIES( this );
		mlt_profile_get()->width = mlt_properties_get_int( properties, "width" );
	}
	else if ( !strcmp( name, "height" ) )
	{
		mlt_properties properties = MLT_CONSUMER_PROPERTIES( this );
		mlt_profile_get()->height = mlt_properties_get_int( properties, "height" );
	}
	else if ( !strcmp( name, "progressive" ) )
	{
		mlt_properties properties = MLT_CONSUMER_PROPERTIES( this );
		mlt_profile_get()->progressive = mlt_properties_get_int( properties, "progressive" );
	}
	else if ( !strcmp( name, "sample_aspect_num" ) )
	{
		mlt_properties properties = MLT_CONSUMER_PROPERTIES( this );
		mlt_profile_get()->sample_aspect_num = mlt_properties_get_int( properties, "sample_aspect_num" );
		mlt_properties_set_double( properties, "aspect_ratio", mlt_profile_sar( NULL )  );
	}
	else if ( !strcmp( name, "sample_aspect_den" ) )
	{
		mlt_properties properties = MLT_CONSUMER_PROPERTIES( this );
		mlt_profile_get()->sample_aspect_den = mlt_properties_get_int( properties, "sample_aspect_den" );
		mlt_properties_set_double( properties, "aspect_ratio", mlt_profile_sar( NULL )  );
	}
	else if ( !strcmp( name, "display_aspect_num" ) )
	{
		mlt_properties properties = MLT_CONSUMER_PROPERTIES( this );
		mlt_profile_get()->display_aspect_num = mlt_properties_get_int( properties, "display_aspect_num" );
		mlt_properties_set_double( properties, "display_ratio", mlt_profile_dar( NULL )  );
	}
	else if ( !strcmp( name, "display_aspect_den" ) )
	{
		mlt_properties properties = MLT_CONSUMER_PROPERTIES( this );
		mlt_profile_get()->display_aspect_den = mlt_properties_get_int( properties, "display_aspect_den" );
		mlt_properties_set_double( properties, "display_ratio", mlt_profile_dar( NULL )  );
	}
}

static void mlt_consumer_frame_show( mlt_listener listener, mlt_properties owner, mlt_service this, void **args )
{
	if ( listener != NULL )
		listener( owner, this, ( mlt_frame )args[ 0 ] );
}

static void mlt_consumer_frame_render( mlt_listener listener, mlt_properties owner, mlt_service this, void **args )
{
	if ( listener != NULL )
		listener( owner, this, ( mlt_frame )args[ 0 ] );
}

/** Create a new consumer.
*/

mlt_consumer mlt_consumer_new( )
{
	// Create the memory for the structure
	mlt_consumer this = malloc( sizeof( struct mlt_consumer_s ) );

	// Initialise it
	if ( this != NULL )
		mlt_consumer_init( this, NULL );

	// Return it
	return this;
}

/** Get the parent service object.
*/

mlt_service mlt_consumer_service( mlt_consumer this )
{
	return this != NULL ? &this->parent : NULL;
}

/** Get the consumer properties.
*/

mlt_properties mlt_consumer_properties( mlt_consumer this )
{
	return this != NULL ? MLT_SERVICE_PROPERTIES( &this->parent ) : NULL;
}

/** Connect the consumer to the producer.
*/

int mlt_consumer_connect( mlt_consumer this, mlt_service producer )
{
	return mlt_service_connect_producer( &this->parent, producer, 0 );
}

/** Start the consumer.
*/

int mlt_consumer_start( mlt_consumer this )
{
	// Stop listening to the property-changed event
	mlt_event_block( g_event_listener );

	// Get the properies
	mlt_properties properties = MLT_CONSUMER_PROPERTIES( this );

	// Determine if there's a test card producer
	char *test_card = mlt_properties_get( properties, "test_card" );

	// Just to make sure nothing is hanging around...
	mlt_frame_close( this->put );
	this->put = NULL;
	this->put_active = 1;

	// Deal with it now.
	if ( test_card != NULL )
	{
		if ( mlt_properties_get_data( properties, "test_card_producer", NULL ) == NULL )
		{
			// Create a test card producer
			mlt_producer producer = mlt_factory_producer( NULL, test_card );

			// Do we have a producer
			if ( producer != NULL )
			{
				// Test card should loop I guess...
				mlt_properties_set( MLT_PRODUCER_PROPERTIES( producer ), "eof", "loop" );
				//mlt_producer_set_speed( producer, 0 );
				//mlt_producer_set_in_and_out( producer, 0, 0 );

				// Set the test card on the consumer
				mlt_properties_set_data( properties, "test_card_producer", producer, 0, ( mlt_destructor )mlt_producer_close, NULL );
			}
		}
	}
	else
	{
		// Allow the hash table to speed things up
		mlt_properties_set_data( properties, "test_card_producer", NULL, 0, NULL, NULL );
	}

	// Check and run an ante command
	if ( mlt_properties_get( properties, "ante" ) )
		system( mlt_properties_get( properties, "ante" ) );

	// Set the real_time preference
	this->real_time = mlt_properties_get_int( properties, "real_time" );

	// Start the service
	if ( this->start != NULL )
		return this->start( this );

	return 0;
}

/** An alternative method to feed frames into the consumer - only valid if
	the consumer itself is not connected.
*/

int mlt_consumer_put_frame( mlt_consumer this, mlt_frame frame )
{
	int error = 1;

	// Get the service assoicated to the consumer
	mlt_service service = MLT_CONSUMER_SERVICE( this );

	if ( mlt_service_producer( service ) == NULL )
	{
		struct timeval now;
		struct timespec tm;
		pthread_mutex_lock( &this->put_mutex );
		while ( this->put_active && this->put != NULL )
		{
			gettimeofday( &now, NULL );
			tm.tv_sec = now.tv_sec + 1;
			tm.tv_nsec = now.tv_usec * 1000;
			pthread_cond_timedwait( &this->put_cond, &this->put_mutex, &tm );
		}
		if ( this->put_active && this->put == NULL )
			this->put = frame;
		else
			mlt_frame_close( frame );
		pthread_cond_broadcast( &this->put_cond );
		pthread_mutex_unlock( &this->put_mutex );
	}
	else
	{
		mlt_frame_close( frame );
	}

	return error;
}

/** Protected method for consumer to get frames from connected service
*/

mlt_frame mlt_consumer_get_frame( mlt_consumer this )
{
	// Frame to return
	mlt_frame frame = NULL;

	// Get the service assoicated to the consumer
	mlt_service service = MLT_CONSUMER_SERVICE( this );

	// Get the consumer properties
	mlt_properties properties = MLT_CONSUMER_PROPERTIES( this );

	// Get the frame
	if ( mlt_service_producer( service ) == NULL && mlt_properties_get_int( properties, "put_mode" ) )
	{
		struct timeval now;
		struct timespec tm;
		pthread_mutex_lock( &this->put_mutex );
		while ( this->put_active && this->put == NULL )
		{
			gettimeofday( &now, NULL );
			tm.tv_sec = now.tv_sec + 1;
			tm.tv_nsec = now.tv_usec * 1000;
			pthread_cond_timedwait( &this->put_cond, &this->put_mutex, &tm );
		}
		frame = this->put;
		this->put = NULL;
		pthread_cond_broadcast( &this->put_cond );
		pthread_mutex_unlock( &this->put_mutex );
		if ( frame != NULL )
			mlt_service_apply_filters( service, frame, 0 );
	}
	else if ( mlt_service_producer( service ) != NULL )
	{
		mlt_service_get_frame( service, &frame, 0 );
	}
	else
	{
		frame = mlt_frame_init( );
	}

	if ( frame != NULL )
	{
		// Get the frame properties
		mlt_properties frame_properties = MLT_FRAME_PROPERTIES( frame );

		// Get the test card producer
		mlt_producer test_card = mlt_properties_get_data( properties, "test_card_producer", NULL );

		// Attach the test frame producer to it.
		if ( test_card != NULL )
			mlt_properties_set_data( frame_properties, "test_card_producer", test_card, 0, NULL, NULL );

		// Attach the rescale property
		mlt_properties_set( frame_properties, "rescale.interp", mlt_properties_get( properties, "rescale" ) );

		// Aspect ratio and other jiggery pokery
		mlt_properties_set_double( frame_properties, "consumer_aspect_ratio", mlt_properties_get_double( properties, "aspect_ratio" ) );
		mlt_properties_set_int( frame_properties, "consumer_deinterlace", mlt_properties_get_int( properties, "progressive" ) | mlt_properties_get_int( properties, "deinterlace" ) );
		mlt_properties_set( frame_properties, "deinterlace_method", mlt_properties_get( properties, "deinterlace_method" ) );
	}

	// Return the frame
	return frame;
}

static inline long time_difference( struct timeval *time1 )
{
	struct timeval time2;
	time2.tv_sec = time1->tv_sec;
	time2.tv_usec = time1->tv_usec;
	gettimeofday( time1, NULL );
	return time1->tv_sec * 1000000 + time1->tv_usec - time2.tv_sec * 1000000 - time2.tv_usec;
}

int mlt_consumer_profile( mlt_properties properties, char *profile )
{
	mlt_profile p = mlt_profile_select( profile );
	if ( p )
	{
		apply_profile_properties( p, properties );
		return 1;
	}
	else
	{
		return 0;
	}
}

static void *consumer_read_ahead_thread( void *arg )
{
	// The argument is the consumer
	mlt_consumer this = arg;

	// Get the properties of the consumer
	mlt_properties properties = MLT_CONSUMER_PROPERTIES( this );

	// Get the width and height
	int width = mlt_properties_get_int( properties, "width" );
	int height = mlt_properties_get_int( properties, "height" );

	// See if video is turned off
	int video_off = mlt_properties_get_int( properties, "video_off" );
	int preview_off = mlt_properties_get_int( properties, "preview_off" );
	int preview_format = mlt_properties_get_int( properties, "preview_format" );

	// Get the audio settings
	mlt_audio_format afmt = mlt_audio_pcm;
	int counter = 0;
	double fps = mlt_properties_get_double( properties, "fps" );
	int channels = mlt_properties_get_int( properties, "channels" );
	int frequency = mlt_properties_get_int( properties, "frequency" );
	int samples = 0;
	int16_t *pcm = NULL;

	// See if audio is turned off
	int audio_off = mlt_properties_get_int( properties, "audio_off" );

	// Get the maximum size of the buffer
	int buffer = mlt_properties_get_int( properties, "buffer" ) + 1;

	// General frame variable
	mlt_frame frame = NULL;
	uint8_t *image = NULL;

	// Time structures
	struct timeval ante;

	// Average time for get_frame and get_image
	int count = 1;
	int skipped = 0;
	int64_t time_wait = 0;
	int64_t time_frame = 0;
	int64_t time_process = 0;
	int skip_next = 0;
	mlt_service lock_object = NULL;

	if ( preview_off && preview_format != 0 )
		this->format = preview_format;

	// Get the first frame
	frame = mlt_consumer_get_frame( this );

	// Get the lock object
	lock_object = mlt_properties_get_data( MLT_FRAME_PROPERTIES( frame ), "consumer_lock_service", NULL );

	// Lock it
	if ( lock_object ) mlt_service_lock( lock_object );

	// Get the image of the first frame
	if ( !video_off )
	{
		mlt_events_fire( MLT_CONSUMER_PROPERTIES( this ), "consumer-frame-render", frame, NULL );
		mlt_frame_get_image( frame, &image, &this->format, &width, &height, 0 );
	}

	if ( !audio_off )
	{
		samples = mlt_sample_calculator( fps, frequency, counter++ );
		mlt_frame_get_audio( frame, &pcm, &afmt, &frequency, &channels, &samples );
	}

	// Unlock the lock object
	if ( lock_object ) mlt_service_unlock( lock_object );

	// Mark as rendered
	mlt_properties_set_int( MLT_FRAME_PROPERTIES( frame ), "rendered", 1 );

	// Get the starting time (can ignore the times above)
	gettimeofday( &ante, NULL );

	// Continue to read ahead
	while ( this->ahead )
	{
		// Fetch width/height again
		width = mlt_properties_get_int( properties, "width" );
		height = mlt_properties_get_int( properties, "height" );

		// Put the current frame into the queue
		pthread_mutex_lock( &this->mutex );
		while( this->ahead && mlt_deque_count( this->queue ) >= buffer )
			pthread_cond_wait( &this->cond, &this->mutex );
		mlt_deque_push_back( this->queue, frame );
		pthread_cond_broadcast( &this->cond );
		pthread_mutex_unlock( &this->mutex );

		time_wait += time_difference( &ante );

		// Get the next frame
		frame = mlt_consumer_get_frame( this );
		time_frame += time_difference( &ante );

		// If there's no frame, we're probably stopped...
		if ( frame == NULL )
			continue;

		// Attempt to fetch the lock object
		lock_object = mlt_properties_get_data( MLT_FRAME_PROPERTIES( frame ), "consumer_lock_service", NULL );

		// Increment the count
		count ++;

		// Lock if there's a lock object
		if ( lock_object ) mlt_service_lock( lock_object );

		// All non normal playback frames should be shown
		if ( mlt_properties_get_int( MLT_FRAME_PROPERTIES( frame ), "_speed" ) != 1 )
		{
			mlt_properties_set_int( MLT_FRAME_PROPERTIES( frame ), "consumer_deinterlace", 1 );
			skipped = 0;
			time_frame = 0;
			time_process = 0;
			time_wait = 0;
			count = 1;
			skip_next = 0;
		}

		// Get the image
		if ( !skip_next )
		{
			// Get the image, mark as rendered and time it
			if ( !video_off )
			{
				mlt_events_fire( MLT_CONSUMER_PROPERTIES( this ), "consumer-frame-render", frame, NULL );
				mlt_frame_get_image( frame, &image, &this->format, &width, &height, 0 );
			}
			mlt_properties_set_int( MLT_FRAME_PROPERTIES( frame ), "rendered", 1 );
		}
		else
		{
			// Increment the number of sequentially skipped frames
			skipped ++;
			skip_next = 0;

			// If we've reached an unacceptable level, reset everything
			if ( skipped > 5 )
			{
				skipped = 0;
				time_frame = 0;
				time_process = 0;
				time_wait = 0;
				count = 1;
			}
		}

		// Always process audio
		if ( !audio_off )
		{
			samples = mlt_sample_calculator( fps, frequency, counter++ );
			mlt_frame_get_audio( frame, &pcm, &afmt, &frequency, &channels, &samples );
		}

		// Increment the time take for this frame
		time_process += time_difference( &ante );

		// Determine if the next frame should be skipped
		if ( mlt_deque_count( this->queue ) <= 5 && ( ( time_wait + time_frame + time_process ) / count ) > 40000 )
			skip_next = 1;

		// Unlock if there's a lock object
		if ( lock_object ) mlt_service_unlock( lock_object );
	}

	// Remove the last frame
	mlt_frame_close( frame );

	return NULL;
}

static void consumer_read_ahead_start( mlt_consumer this )
{
	// We're running now
	this->ahead = 1;

	// Create the frame queue
	this->queue = mlt_deque_init( );

	// Create the mutex
	pthread_mutex_init( &this->mutex, NULL );

	// Create the condition
	pthread_cond_init( &this->cond, NULL );

	// Create the read ahead 
	pthread_create( &this->ahead_thread, NULL, consumer_read_ahead_thread, this );
}

static void consumer_read_ahead_stop( mlt_consumer this )
{
	// Make sure we're running
	if ( this->ahead )
	{
		// Inform thread to stop
		this->ahead = 0;

		// Broadcast to the condition in case it's waiting
		pthread_mutex_lock( &this->mutex );
		pthread_cond_broadcast( &this->cond );
		pthread_mutex_unlock( &this->mutex );

		// Broadcast to the put condition in case it's waiting
		pthread_mutex_lock( &this->put_mutex );
		pthread_cond_broadcast( &this->put_cond );
		pthread_mutex_unlock( &this->put_mutex );

		// Join the thread
		pthread_join( this->ahead_thread, NULL );

		// Destroy the mutex
		pthread_mutex_destroy( &this->mutex );

		// Destroy the condition
		pthread_cond_destroy( &this->cond );

		// Wipe the queue
		while ( mlt_deque_count( this->queue ) )
			mlt_frame_close( mlt_deque_pop_back( this->queue ) );

		// Close the queue
		mlt_deque_close( this->queue );
	}
}

void mlt_consumer_purge( mlt_consumer this )
{
	if ( this->ahead )
	{
		pthread_mutex_lock( &this->mutex );
		while ( mlt_deque_count( this->queue ) )
			mlt_frame_close( mlt_deque_pop_back( this->queue ) );
		pthread_cond_broadcast( &this->cond );
		pthread_mutex_unlock( &this->mutex );
	}
}

mlt_frame mlt_consumer_rt_frame( mlt_consumer this )
{
	// Frame to return
	mlt_frame frame = NULL;

	// Get the properties
	mlt_properties properties = MLT_CONSUMER_PROPERTIES( this );

	// Check if the user has requested real time or not
	if ( this->real_time )
	{
		int size = 1;

		// Is the read ahead running?
		if ( this->ahead == 0 )
		{
			int buffer = mlt_properties_get_int( properties, "buffer" );
			int prefill = mlt_properties_get_int( properties, "prefill" );
			consumer_read_ahead_start( this );
			if ( buffer > 1 )
				size = prefill > 0 && prefill < buffer ? prefill : buffer;
		}
	
		// Get frame from queue
		pthread_mutex_lock( &this->mutex );
		while( this->ahead && mlt_deque_count( this->queue ) < size )
			pthread_cond_wait( &this->cond, &this->mutex );
		frame = mlt_deque_pop_front( this->queue );
		pthread_cond_broadcast( &this->cond );
		pthread_mutex_unlock( &this->mutex );
	}
	else
	{
		// Get the frame in non real time
		frame = mlt_consumer_get_frame( this );

		// This isn't true, but from the consumers perspective it is
		if ( frame != NULL )
			mlt_properties_set_int( MLT_FRAME_PROPERTIES( frame ), "rendered", 1 );
	}

	return frame;
}

/** Callback for the implementation to indicate a stopped condition.
*/

void mlt_consumer_stopped( mlt_consumer this )
{
	mlt_properties_set_int( MLT_CONSUMER_PROPERTIES( this ), "running", 0 );
	mlt_events_fire( MLT_CONSUMER_PROPERTIES( this ), "consumer-stopped", NULL );
	mlt_event_unblock( g_event_listener );
}

/** Stop the consumer.
*/

int mlt_consumer_stop( mlt_consumer this )
{
	// Get the properies
	mlt_properties properties = MLT_CONSUMER_PROPERTIES( this );
	char *debug = mlt_properties_get( MLT_CONSUMER_PROPERTIES( this ), "debug" );

	// Just in case...
	if ( debug ) fprintf( stderr, "%s: stopping put waiting\n", debug );
	pthread_mutex_lock( &this->put_mutex );
	this->put_active = 0;
	pthread_cond_broadcast( &this->put_cond );
	pthread_mutex_unlock( &this->put_mutex );

	// Stop the consumer
	if ( debug ) fprintf( stderr, "%s: stopping consumer\n", debug );
	if ( this->stop != NULL )
		this->stop( this );

	// Check if the user has requested real time or not and stop if necessary
	if ( debug ) fprintf( stderr, "%s: stopping read_ahead\n", debug );
	if ( mlt_properties_get_int( properties, "real_time" ) )
		consumer_read_ahead_stop( this );

	// Kill the test card
	mlt_properties_set_data( properties, "test_card_producer", NULL, 0, NULL, NULL );

	// Check and run a post command
	if ( mlt_properties_get( properties, "post" ) )
		system( mlt_properties_get( properties, "post" ) );

	if ( debug ) fprintf( stderr, "%s: stopped\n", debug );

	return 0;
}

/** Determine if the consumer is stopped.
*/

int mlt_consumer_is_stopped( mlt_consumer this )
{
	// Check if the consumer is stopped
	if ( this->is_stopped != NULL )
		return this->is_stopped( this );

	return 0;
}

/** Close the consumer.
*/

void mlt_consumer_close( mlt_consumer this )
{
	if ( this != NULL && mlt_properties_dec_ref( MLT_CONSUMER_PROPERTIES( this ) ) <= 0 )
	{
		// Get the childs close function
		void ( *consumer_close )( ) = this->close;

		if ( consumer_close )
		{
			// Just in case...
			//mlt_consumer_stop( this );

			this->close = NULL;
			consumer_close( this );
		}
		else
		{
			// Make sure it only gets called once
			this->parent.close = NULL;

			// Destroy the push mutex and condition
			pthread_mutex_destroy( &this->put_mutex );
			pthread_cond_destroy( &this->put_cond );

			mlt_service_close( &this->parent );
		}
	}
}
