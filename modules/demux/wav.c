/*****************************************************************************
 * wav.c : wav file input module for vlc
 *****************************************************************************
 * Copyright (C) 2001-2003 VideoLAN
 * $Id$
 *
 * Authors: Laurent Aimar <fenrir@via.ecp.fr>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111, USA.
 *****************************************************************************/

/*****************************************************************************
 * Preamble
 *****************************************************************************/
#include <stdlib.h>                                      /* malloc(), free() */

#include <vlc/vlc.h>
#include <vlc/input.h>

#include <codecs.h>

/*****************************************************************************
 * Module descriptor
 *****************************************************************************/
static int  Open ( vlc_object_t * );
static void Close( vlc_object_t * );

vlc_module_begin();
    set_description( _("WAV demuxer") );
    set_capability( "demux2", 142 );
    set_callbacks( Open, Close );
vlc_module_end();

/*****************************************************************************
 * Local prototypes
 *****************************************************************************/
static int Demux  ( demux_t * );
static int Control( demux_t *, int i_query, va_list args );

struct demux_sys_t
{
    es_format_t     fmt;
    es_out_id_t     *p_es;

    int64_t         i_data_pos;
    unsigned int    i_data_size;

    mtime_t         i_time;
    unsigned int    i_frame_size;
    mtime_t         i_frame_length;
};

#define __EVEN( x ) ( ( (x)%2 != 0 ) ? ((x)+1) : (x) )

static int ChunkFind( demux_t *, char *, unsigned int * );

static void FrameInfo_IMA_ADPCM( demux_t *, unsigned int *, mtime_t * );
static void FrameInfo_MS_ADPCM ( demux_t *, unsigned int *, mtime_t * );
static void FrameInfo_PCM      ( demux_t *, unsigned int *, mtime_t * );

/*****************************************************************************
 * Open: check file and initializes structures
 *****************************************************************************/
static int Open( vlc_object_t * p_this )
{
    demux_t     *p_demux = (demux_t*)p_this;
    demux_sys_t *p_sys;

    uint8_t     *p_peek;
    unsigned int i_size, i_extended;
    char        *psz_name;

    WAVEFORMATEXTENSIBLE *p_wf_ext;
    WAVEFORMATEX         *p_wf;

    /* Is it a wav file ? */
    if( stream_Peek( p_demux->s, &p_peek, 12 ) < 12 )
    {
        msg_Warn( p_demux, "WAV module discarded (cannot peek)" );
        return VLC_EGENERIC;
    }
    if( strncmp( p_peek, "RIFF", 4 ) || strncmp( &p_peek[8], "WAVE", 4 ) )
    {
        return VLC_EGENERIC;
    }

    p_demux->pf_demux   = Demux;
    p_demux->pf_control = Control;
    p_demux->p_sys      = p_sys = malloc( sizeof( demux_sys_t ) );
    p_sys->p_es         = NULL;
    p_sys->i_time       = 1;

    /* skip riff header */
    stream_Read( p_demux->s, NULL, 12 );  /* cannot fail as peek succeed */

    /* search fmt chunk */
    if( ChunkFind( p_demux, "fmt ", &i_size ) )
    {
        msg_Err( p_demux, "cannot find 'fmt ' chunk" );
        goto error;
    }
    if( i_size < sizeof( WAVEFORMATEX ) - 2 )   /* XXX -2 isn't a typo */
    {
        msg_Err( p_demux, "invalid 'fmt ' chunk" );
        goto error;
    }
    stream_Read( p_demux->s, NULL, 8 );   /* Cannot fail */

    /* load waveformatex */
    p_wf_ext = malloc( __EVEN( i_size ) + 2 );
    p_wf = (WAVEFORMATEX *)p_wf_ext;
    p_wf->cbSize = 0;
    if( stream_Read( p_demux->s,
                     p_wf, __EVEN( i_size ) ) < (int)__EVEN( i_size ) )
    {
        msg_Err( p_demux, "cannot load 'fmt ' chunk" );
        goto error;
    }

    es_format_Init( &p_sys->fmt, AUDIO_ES, 0 );
    wf_tag_to_fourcc( GetWLE( &p_wf->wFormatTag ), &p_sys->fmt.i_codec,
                      &psz_name );
    p_sys->fmt.audio.i_channels = GetWLE ( &p_wf->nChannels );
    p_sys->fmt.audio.i_rate = GetDWLE( &p_wf->nSamplesPerSec );
    p_sys->fmt.audio.i_blockalign = GetWLE( &p_wf->nBlockAlign );
    p_sys->fmt.i_bitrate = GetDWLE( &p_wf->nAvgBytesPerSec ) * 8;
    p_sys->fmt.audio.i_bitspersample = GetWLE( &p_wf->wBitsPerSample );
    p_sys->fmt.i_extra = GetWLE( &p_wf->cbSize );
    i_extended = 0;

    /* Handle new WAVE_FORMAT_EXTENSIBLE wav files */
    if( GetWLE( &p_wf->wFormatTag ) == WAVE_FORMAT_EXTENSIBLE &&
        i_size >= sizeof( WAVEFORMATEXTENSIBLE ) )
    {
        wf_tag_to_fourcc( GetWLE( &p_wf_ext->SubFormat ),
                          &p_sys->fmt.i_codec, &psz_name );
        i_extended = sizeof( WAVEFORMATEXTENSIBLE ) - sizeof( WAVEFORMATEX );
        p_sys->fmt.i_extra -= i_extended;
    }

    if( p_sys->fmt.i_extra > 0 )
    {
        p_sys->fmt.p_extra = malloc( p_sys->fmt.i_extra );
        memcpy( p_sys->fmt.p_extra, ((uint8_t *)p_wf) + i_extended,
                p_sys->fmt.i_extra );
    }

    msg_Dbg( p_demux, "format: 0x%4.4x, fourcc: %4.4s, channels: %d, "
             "freq: %d Hz, bitrate: %dKo/s, blockalign: %d, bits/samples: %d, "
             "extra size: %d",
             GetWLE( &p_wf->wFormatTag ), (char *)&p_sys->fmt.i_codec,
             p_sys->fmt.audio.i_channels, p_sys->fmt.audio.i_rate,
             p_sys->fmt.i_bitrate / 8 / 1024, p_sys->fmt.audio.i_blockalign,
             p_sys->fmt.audio.i_bitspersample, p_sys->fmt.i_extra );

    free( p_wf );

    switch( p_sys->fmt.i_codec )
    {
    case VLC_FOURCC( 'a', 'r', 'a', 'w' ):
    case VLC_FOURCC( 'a', 'f', 'l', 't' ):
    case VLC_FOURCC( 'u', 'l', 'a', 'w' ):
    case VLC_FOURCC( 'a', 'l', 'a', 'w' ):
        FrameInfo_PCM( p_demux, &p_sys->i_frame_size, &p_sys->i_frame_length );
        break;
    case VLC_FOURCC( 'm', 's', 0x00, 0x02 ):
        FrameInfo_MS_ADPCM( p_demux, &p_sys->i_frame_size,
                            &p_sys->i_frame_length );
        break;
    case VLC_FOURCC( 'm', 's', 0x00, 0x11 ):
        FrameInfo_IMA_ADPCM( p_demux, &p_sys->i_frame_size,
                             &p_sys->i_frame_length );
        break;
    case VLC_FOURCC( 'm', 's', 0x00, 0x61 ):
    case VLC_FOURCC( 'm', 's', 0x00, 0x62 ):
        /* FIXME not sure at all FIXME */
        FrameInfo_MS_ADPCM( p_demux, &p_sys->i_frame_size,
                            &p_sys->i_frame_length );
        break;
    case VLC_FOURCC( 'm', 'p', 'g', 'a' ):
    case VLC_FOURCC( 'a', '5', '2', ' ' ):
        /* FIXME set end of area FIXME */
        goto relay;
    default:
        msg_Err( p_demux, "unsupported codec (%4.4s)",
                 (char*)&p_sys->fmt.i_codec );
        goto error;
    }

    msg_Dbg( p_demux, "found %s audio format", psz_name );

    if( ChunkFind( p_demux, "data", &p_sys->i_data_size ) )
    {
        msg_Err( p_demux, "cannot find 'data' chunk" );
        goto error;
    }
    stream_Read( p_demux->s, NULL, 8 );   /* Cannot fail */
    p_sys->i_data_pos = stream_Tell( p_demux->s );

    if( p_sys->fmt.i_bitrate <= 0 )
    {
        p_sys->fmt.i_bitrate = (mtime_t)p_sys->i_frame_size *
            I64C(8000000) / p_sys->i_frame_length;
    }

    p_sys->p_es = es_out_Add( p_demux->out, &p_sys->fmt );
    return VLC_SUCCESS;

error:
relay:
    free( p_sys );
    return VLC_EGENERIC;
}

/*****************************************************************************
 * Demux: read packet and send them to decoders
 *****************************************************************************
 * Returns -1 in case of error, 0 in case of EOF, 1 otherwise
 *****************************************************************************/
static int Demux( demux_t *p_demux )
{
    demux_sys_t *p_sys = p_demux->p_sys;
    int64_t     i_pos;
    block_t     *p_block;

    i_pos = stream_Tell( p_demux->s );

    if( p_sys->i_data_size > 0 &&
        i_pos >= p_sys->i_data_pos + p_sys->i_data_size )
    {
        /* EOF */
        return 0;
    }

    if( ( p_block = stream_Block( p_demux->s, p_sys->i_frame_size ) ) == NULL )
    {
        msg_Warn( p_demux, "cannot read data" );
        return 0;
    }
    p_block->i_dts = p_block->i_pts = p_sys->i_time;

    /* set PCR */
    es_out_Control( p_demux->out, ES_OUT_SET_PCR, p_sys->i_time );

    es_out_Send( p_demux->out, p_sys->p_es, p_block );

    p_sys->i_time += p_sys->i_frame_length;

    return 1;
}

/*****************************************************************************
 * Close: frees unused data
 *****************************************************************************/
static void Close ( vlc_object_t * p_this )
{
    demux_t     *p_demux = (demux_t*)p_this;
    demux_sys_t *p_sys  = p_demux->p_sys;

    free( p_sys );
}

/*****************************************************************************
 * Control:
 *****************************************************************************/
static int Control( demux_t *p_demux, int i_query, va_list args )
{
    demux_sys_t *p_sys  = p_demux->p_sys;
    int64_t i_end = -1;
    if( p_sys->i_data_size > 0 )
    {
        i_end = p_sys->i_data_pos + p_sys->i_data_size;
    }
    return demux2_vaControlHelper( p_demux->s,
                                   p_sys->i_data_pos, i_end,
                                   p_sys->fmt.i_bitrate, p_sys->fmt.audio.i_blockalign,
                                   i_query, args );
}

/*****************************************************************************
 * Local functions
 *****************************************************************************/
static int ChunkFind( demux_t *p_demux,
                      char *fcc, unsigned int *pi_size )
{
    uint8_t *p_peek;

    for( ;; )
    {
        int i_size;

        if( stream_Peek( p_demux->s, &p_peek, 8 ) < 8 )
        {
            msg_Err( p_demux, "cannot peek()" );
            return VLC_EGENERIC;
        }

        i_size = GetDWLE( p_peek + 4 );

        msg_Dbg( p_demux, "Chunk: fcc=`%4.4s` size=%d", p_peek, i_size );

        if( !strncmp( p_peek, fcc, 4 ) )
        {
            if( pi_size )
            {
                *pi_size = i_size;
            }
            return VLC_SUCCESS;
        }

        i_size = __EVEN( i_size ) + 8;
        if( stream_Read( p_demux->s, NULL, i_size ) != i_size )
        {
            return VLC_EGENERIC;
        }
    }
}

static void FrameInfo_PCM( demux_t      *p_demux,
                           unsigned int *pi_size,
                           mtime_t      *pi_length )
{
    demux_sys_t *p_sys = p_demux->p_sys;

    int i_samples;

    int i_bytes;
    int i_modulo;

    /* read samples for 50ms of */
    i_samples = __MAX( p_sys->fmt.audio.i_rate / 20, 1 );


    *pi_length = (mtime_t)1000000 *
                 (mtime_t)i_samples /
                 (mtime_t)p_sys->fmt.audio.i_rate;

    i_bytes = i_samples * p_sys->fmt.audio.i_channels *
        ( (p_sys->fmt.audio.i_bitspersample + 7) / 8 );

    if( p_sys->fmt.audio.i_blockalign > 0 )
    {
        if( ( i_modulo = i_bytes % p_sys->fmt.audio.i_blockalign ) != 0 )
        {
            i_bytes += p_sys->fmt.audio.i_blockalign - i_modulo;
        }
    }

    *pi_size = i_bytes;
}

static void FrameInfo_MS_ADPCM( demux_t      *p_demux,
                                unsigned int *pi_size,
                                mtime_t      *pi_length )
{
    demux_sys_t *p_sys = p_demux->p_sys;

    int i_samples;

    i_samples = 2 + 2 * ( p_sys->fmt.audio.i_blockalign -
        7 * p_sys->fmt.audio.i_channels ) / p_sys->fmt.audio.i_channels;

    *pi_length = (mtime_t)1000000 * (mtime_t)i_samples /
                 (mtime_t)p_sys->fmt.audio.i_rate;

    *pi_size = p_sys->fmt.audio.i_blockalign;
}

static void FrameInfo_IMA_ADPCM( demux_t      *p_demux,
                                 unsigned int *pi_size,
                                 mtime_t      *pi_length )
{
    demux_sys_t *p_sys = p_demux->p_sys;

    int i_samples;

    i_samples = 2 * ( p_sys->fmt.audio.i_blockalign -
        4 * p_sys->fmt.audio.i_channels ) / p_sys->fmt.audio.i_channels;

    *pi_length = (mtime_t)1000000 * (mtime_t)i_samples /
                 (mtime_t)p_sys->fmt.audio.i_rate;

    *pi_size = p_sys->fmt.audio.i_blockalign;
}


