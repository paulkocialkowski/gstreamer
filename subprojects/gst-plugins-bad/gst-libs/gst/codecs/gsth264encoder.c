/* GStreamer
 * Copyright (C) 2023 Michael Grzeschik <m.grzeschik@pengutronix.de>
 * Copyright (C) 2023 Denis Shimizu <denis.shimizu@collabora.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gsth264encoder.h"
#include "gstratecontroller.h"
#include <gst/codecparsers/gsth264bitwriter.h>
#include <gst/video/gstvideometa.h>

GST_DEBUG_CATEGORY (gst_h264_encoder_debug);
#define GST_CAT_DEFAULT gst_h264_encoder_debug

#define H264_MAX_QP	            51
#define H264_MIN_QP                  0

#define DEFAULT_KEYFRAME_INTERVAL   30
#define DEFAULT_MAX_QP              51
#define DEFAULT_MIN_QP              10
#define DEFAULT_QP_STEP              4
#define DEFAULT_QUANTIZER           18
#define DEFAULT_BITRATE      G_MAXUINT

enum
{
  PROP_0,
  PROP_KEYFRAME_INTERVAL,
  PROP_MAX_QP,
  PROP_MIN_QP,
  PROP_QP_STEP,
  PROP_QUANTIZER,
  PROP_BITRATE,
  PROP_CABAC,
  PROP_CABAC_INIT_IDC,
  PROP_RATE_CONTROL,
};

struct _GstH264EncoderPrivate
{
  guint32 last_keyframe;
  GstRateController *rate_controller;

  /* properties */
  gint keyframe_interval;
  gboolean cabac;
  guint cabac_init_idc;
};

#define parent_class gst_h264_encoder_parent_class
G_DEFINE_ABSTRACT_TYPE_WITH_CODE (GstH264Encoder, gst_h264_encoder,
    GST_TYPE_VIDEO_ENCODER,
    G_ADD_PRIVATE (GstH264Encoder);
    GST_DEBUG_CATEGORY_INIT (gst_h264_encoder_debug, "h264encoder", 0,
        "H264 Video Encoder"));

static void
gst_h264_encoder_init (GstH264Encoder * self)
{
  self->priv = gst_h264_encoder_get_instance_private (self);
  self->priv->rate_controller = gst_rc_new ();
}

static void
gst_h264_encoder_finalize (GObject * object)
{
  GstH264Encoder *self = GST_H264_ENCODER (object);

  gst_object_unref (self->priv->rate_controller);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static gboolean
gst_h264_encoder_start (GstVideoEncoder * encoder)
{
  return TRUE;
}

static gboolean
gst_h264_encoder_stop (GstVideoEncoder * encoder)
{
  GstH264Encoder *self = GST_H264_ENCODER (encoder);
  GstH264EncoderPrivate *priv = self->priv;

  priv->last_keyframe = 0;

  return TRUE;
}

static gboolean
gst_h264_encoder_set_format (GstVideoEncoder * encoder,
    GstVideoCodecState * state)
{
  GstH264Encoder *self = GST_H264_ENCODER (encoder);
  GstH264EncoderPrivate *priv = self->priv;

  gst_rc_set_format (priv->rate_controller, &state->info);

  return TRUE;
}

static GstFlowReturn
gst_h264_encoder_set_frame_type (GstH264Encoder * self,
    GstH264Frame * h264_frame)
{
  GstH264EncoderPrivate *priv = self->priv;
  GstVideoCodecFrame *frame = h264_frame->frame;

  if (GST_VIDEO_CODEC_FRAME_IS_FORCE_KEYFRAME (frame)) {
    h264_frame->type = GstH264Keyframe;
    return GST_FLOW_OK;
  }

  GST_DEBUG_OBJECT (self, "system frame number %d last keyframe %d interval %d\n", frame->system_frame_number, priv->last_keyframe, priv->keyframe_interval);

  if ((frame->system_frame_number - priv->last_keyframe) >=
      priv->keyframe_interval || frame->system_frame_number == 0) {
    /* Generate a keyframe */
    GST_DEBUG_OBJECT (self, "Generate a keyframe");
    h264_frame->type = GstH264Keyframe;
    return GST_FLOW_OK;
  }

  /* Generate a interframe */
  GST_DEBUG_OBJECT (self, "Generate a interframe");
  h264_frame->type = GstH264Inter;
  return GST_FLOW_OK;
}

static void
gst_h264_encoder_mark_frame (GstH264Encoder * self, GstH264Frame * h264_frame)
{
  GstVideoCodecFrame *frame = h264_frame->frame;
  GstH264EncoderPrivate *priv = self->priv;
  GstRcFrameType rc_frame_type = GST_RC_INTER_FRAME;

  switch (h264_frame->type) {
    case GstH264Keyframe:
      priv->last_keyframe = frame->system_frame_number;
      rc_frame_type = GST_RC_KEY_FRAME;
      break;
    default:
      break;
  }

  gst_rc_record (priv->rate_controller, rc_frame_type,
      gst_buffer_get_size (frame->output_buffer), frame->duration);
}

static GstFlowReturn
gst_h264_encoder_handle_frame (GstVideoEncoder * encoder,
    GstVideoCodecFrame * frame)
{
  GstH264Encoder *self = GST_H264_ENCODER (encoder);
  GstH264EncoderPrivate *priv = self->priv;
  GstH264EncoderClass *klass = GST_H264_ENCODER_GET_CLASS (self);
  GstFlowReturn ret = GST_FLOW_OK;
  GstH264Frame *h264_frame = gst_h264_frame_new (frame);

  ret = gst_h264_encoder_set_frame_type (self, h264_frame);
  if (ret != GST_FLOW_OK)
    return ret;

  h264_frame->qp = gst_rc_get_qp (priv->rate_controller);

  /* Send the frame to encode */
  if (klass->encode_frame) {
    ret = klass->encode_frame (self, h264_frame);
    if (ret == GST_FLOW_OK)
      gst_h264_encoder_mark_frame (self, h264_frame);
  }

  gst_h264_frame_unref (h264_frame);

  return ret;
}

static void
gst_h264_encoder_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  GstH264Encoder *self = GST_H264_ENCODER (object);
  GstH264EncoderPrivate *priv = self->priv;

  GST_OBJECT_LOCK (self);

  switch (property_id) {
    case PROP_KEYFRAME_INTERVAL:
      g_value_set_int (value, priv->keyframe_interval);
      break;
    case PROP_MAX_QP:
      g_value_set_int (value, gst_rc_get_max_qp (priv->rate_controller));
      break;
    case PROP_MIN_QP:
      g_value_set_int (value, gst_rc_get_min_qp (priv->rate_controller));
      break;
    case PROP_QP_STEP:
      g_value_set_int (value, gst_rc_get_qp_step (priv->rate_controller));
      break;
    case PROP_QUANTIZER:
      g_value_set_int (value, gst_rc_get_init_qp (priv->rate_controller));
      break;
    case PROP_BITRATE:
      g_value_set_uint (value, gst_rc_get_bitrate (priv->rate_controller));
      break;
    case PROP_RATE_CONTROL:
      g_value_set_enum (value, gst_rc_get_mode (priv->rate_controller));
      break;
    case PROP_CABAC:
      g_value_set_boolean (value, priv->cabac);
      break;
    case PROP_CABAC_INIT_IDC:
      g_value_set_uint (value, priv->cabac_init_idc);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }

  GST_OBJECT_UNLOCK (self);
}

static void
gst_h264_encoder_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  GstH264Encoder *self = GST_H264_ENCODER (object);
  GstH264EncoderPrivate *priv = self->priv;

  GST_OBJECT_LOCK (self);

  switch (property_id) {
    case PROP_KEYFRAME_INTERVAL:
      priv->keyframe_interval = g_value_get_int (value);
      break;
    case PROP_MAX_QP:
      gst_rc_set_max_qp (priv->rate_controller, g_value_get_int (value));
      break;
    case PROP_MIN_QP:
      gst_rc_set_min_qp (priv->rate_controller, g_value_get_int (value));
      break;
    case PROP_QP_STEP:
      gst_rc_set_qp_step (priv->rate_controller, g_value_get_int (value));
      break;
    case PROP_QUANTIZER:
      gst_rc_set_init_qp (priv->rate_controller, g_value_get_int (value));
      break;
    case PROP_BITRATE:
      gst_rc_set_bitrate (priv->rate_controller, g_value_get_uint (value));
      break;
    case PROP_RATE_CONTROL:
      gst_rc_set_mode (priv->rate_controller, g_value_get_enum (value));
      break;
    case PROP_CABAC:
      priv->cabac = g_value_get_boolean (value);
      break;
    case PROP_CABAC_INIT_IDC:
      priv->cabac_init_idc = g_value_get_uint (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }

  GST_OBJECT_UNLOCK (self);
}

static void
gst_h264_encoder_class_init (GstH264EncoderClass * klass)
{
  GstVideoEncoderClass *encoder_class = GST_VIDEO_ENCODER_CLASS (klass);
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = GST_DEBUG_FUNCPTR (gst_h264_encoder_finalize);
  object_class->get_property = gst_h264_encoder_get_property;
  object_class->set_property = gst_h264_encoder_set_property;

  encoder_class->start = GST_DEBUG_FUNCPTR (gst_h264_encoder_start);
  encoder_class->stop = GST_DEBUG_FUNCPTR (gst_h264_encoder_stop);
  encoder_class->set_format = GST_DEBUG_FUNCPTR (gst_h264_encoder_set_format);
  encoder_class->handle_frame =
      GST_DEBUG_FUNCPTR (gst_h264_encoder_handle_frame);

  /**
   * GstH264Encoder:keyframe-interval:
   *
   * Since: 1.2x
   */
  g_object_class_install_property (object_class, PROP_KEYFRAME_INTERVAL,
      g_param_spec_int ("keyframe-interval", "Keyframe Interval",
          "Maximum distance in frames between IDR.",
          0, G_MAXINT, DEFAULT_KEYFRAME_INTERVAL,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_CONSTRUCT));

  /**
   * GstH264Encoder:qp-max:
   *
   * Since: 1.2x
   */
  g_object_class_install_property (object_class, PROP_MAX_QP,
      g_param_spec_int ("qp-max", "Max Quantizer Level",
          "Set upper qp limit (lower number equates to higher quality but more bits)",
          H264_MIN_QP, H264_MAX_QP, DEFAULT_MAX_QP,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_CONSTRUCT));

   /**
   * GstH264Encoder:qp-min:
   *
   * Since: 1.2x
   */
  g_object_class_install_property (object_class, PROP_MIN_QP,
      g_param_spec_int ("qp-min", "Min Quantizer Level",
          "Set lower qp limit (lower number equates to higher quality but more bits)",
          H264_MIN_QP, H264_MAX_QP, DEFAULT_MIN_QP,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_CONSTRUCT));

  /**
   * GstH264Encoder:qp-step:
   *
   * Since: 1.2x
   */
  g_object_class_install_property (object_class, PROP_QP_STEP,
      g_param_spec_int ("qp-step", "Max QP increase/decrease step",
          "Set maximum value which qp value can be increase/decrease by the bitrate controller (Valid only with rate-control=cbr)",
          H264_MIN_QP, H264_MAX_QP, DEFAULT_QP_STEP,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_CONSTRUCT));

 /**
   * GstH264Encoder:quantizer:
   *
   * Since: 1.2x
   */
  g_object_class_install_property (object_class, PROP_QUANTIZER,
      g_param_spec_int ("quantizer", "Quantizer Level",
          "Set the qp value (lower number equates to higher quality but more bits, initial value for rate-control=cbr)",
          H264_MIN_QP, H264_MAX_QP, DEFAULT_QUANTIZER,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_CONSTRUCT));

   /**
   * GstH264Encoder:bitrate:
   *
   * Since: 1.2x
   */
  g_object_class_install_property (object_class, PROP_BITRATE,
      g_param_spec_uint ("bitrate", "Targeted bitrate",
          "Set the targeted bitrate (in bit/s)",
          0, UINT_MAX, DEFAULT_BITRATE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_CONSTRUCT));

  /**
  * GstH264Encoder:cabac:
  * Note: Supported only on main profile
  *
  * Since: 1.2x
  */
  g_object_class_install_property (object_class, PROP_CABAC,
      g_param_spec_boolean ("cabac", "CABAC",
          "Enable Cabac", FALSE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  /**
  * GstH264Encoder:cabac-init-idc:
  * Note: Supported only on main profile
  *
  * Since: 1.2x
  */
  g_object_class_install_property (object_class, PROP_CABAC_INIT_IDC,
      g_param_spec_uint ("cabac-init-idc", "PROP_CABAC_INIT_IDC",
          "Set Cabac init idc value",
          0, 2, 0,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_CONSTRUCT));

  /**
  * GstH264Encoder:rate-control:
  *
  * Since: 1.2x
  */
  g_object_class_install_property (object_class, PROP_RATE_CONTROL,
      g_param_spec_enum ("rate-control", "Rate Control Mode",
          "Select rate control mode", gst_rate_control_mode_get_type (),
          GST_RC_CONSTANT_QP,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_CONSTRUCT));
}
