/* GStreamer
 * Copyright (C) 2022 Benjamin Gaignard <benjamin.gaignard@collabora.com>
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

#ifndef __GST_V4L2_ENCODER_H__
#define __GST_V4L2_ENCODER_H__

#include <gst/gst.h>
#include <gst/video/video.h>

#include "gstv4l2codecdevice.h"
#include "linux/videodev2.h"

G_BEGIN_DECLS

#define GST_TYPE_V4L2_ENCODER gst_v4l2_encoder_get_type ()
G_DECLARE_FINAL_TYPE (GstV4l2Encoder, gst_v4l2_encoder, GST, V4L2_ENCODER, GstObject);

typedef struct _GstV4l2Request GstV4l2Request;

GstV4l2Encoder *  gst_v4l2_encoder_new (GstV4l2CodecDevice * device);

guint             gst_v4l2_encoder_get_version (GstV4l2Encoder * self);

gboolean          gst_v4l2_encoder_open (GstV4l2Encoder * decoder);

gboolean          gst_v4l2_encoder_close (GstV4l2Encoder * decoder);

gboolean          gst_v4l2_encoder_streamon (GstV4l2Encoder * self,
                                             GstPadDirection direction);

gboolean          gst_v4l2_encoder_streamoff (GstV4l2Encoder * self,
                                              GstPadDirection direction);

gboolean          gst_v4l2_encoder_flush (GstV4l2Encoder * self);

gboolean          gst_v4l2_encoder_enum_sink_fmt (GstV4l2Encoder * self,
                                                  gint i, guint32 * out_fmt);

GstCaps *         gst_v4l2_encoder_list_sink_formats (GstV4l2Encoder * self);

gboolean          gst_v4l2_encoder_select_sink_format (GstV4l2Encoder * self,
                                                       GstCaps * caps,
                                                       GstVideoInfoDmaDrm * vinfo_drm);

gboolean          gst_v4l2_encoder_enum_src_formats (GstV4l2Encoder * self,
                                                     gint i, guint32 * out_fmt);

gboolean          gst_v4l2_encoder_set_src_fmt (GstV4l2Encoder * self,
                                                GstVideoInfoDmaDrm * info,
                                                guint32 pix_fmt);

gboolean          gst_v4l2_encoder_set_crop (GstV4l2Encoder * self,
                                             const GstVideoInfo * info);

gboolean          gst_v4l2_encoder_set_fps (GstV4l2Encoder * self,
                                            const GstVideoInfo * info);

gint              gst_v4l2_encoder_request_buffers (GstV4l2Encoder * self,
                                                    GstPadDirection direction,
                                                    guint num_buffers,
                                                    guint mem_type);

gboolean          gst_v4l2_encoder_export_buffer (GstV4l2Encoder * self,
                                                  GstPadDirection directon,
                                                  gint index,
                                                  gint * fds,
                                                  gsize * sizes,
                                                  gsize * offsets,
                                                  guint *num_fds);

gboolean          gst_v4l2_encoder_set_controls (GstV4l2Encoder * self,
                                                 GstV4l2Request * request,
                                                 struct v4l2_ext_control *control,
                                                 guint count);

gboolean          gst_v4l2_encoder_get_controls (GstV4l2Encoder * self,
                                                 GstV4l2Request * request,
                                                 struct v4l2_ext_control * control,
                                                 guint count);

gboolean          gst_v4l2_encoder_query_control_size (GstV4l2Encoder * self,
                                                 unsigned int control_id,
						 unsigned int *control_size);

void              gst_v4l2_encoder_install_properties (GObjectClass * gobject_class,
                                                       gint prop_offset,
                                                       GstV4l2CodecDevice * device);

void              gst_v4l2_encoder_set_property (GObject * object, guint prop_id,
                                                 const GValue * value, GParamSpec * pspec);

void              gst_v4l2_encoder_get_property (GObject * object, guint prop_id,
                                                 GValue * value, GParamSpec * pspec);

void              gst_v4l2_encoder_register (GstPlugin * plugin,
                                             GType dec_type,
                                             GClassInitFunc class_init,
                                             gconstpointer class_data,
                                             GInstanceInitFunc instance_init,
                                             const gchar *element_name_tmpl,
                                             GstV4l2CodecDevice * device,
                                             guint rank,
                                             gchar ** element_name);

GstV4l2Request   *gst_v4l2_encoder_alloc_request (GstV4l2Encoder * self,
                                                  guint32 frame_num,
						  GstBuffer * pic_buf,
                                                  GstBuffer * bitstream);

GstV4l2Request   *gst_v4l2_encoder_alloc_ro_request (GstV4l2Encoder * self);

GstV4l2Request   *gst_v4l2_encoder_alloc_sub_request (GstV4l2Encoder * self,
                                                      GstV4l2Request * prev_request,
                                                      GstBuffer * bitstream);

GstV4l2Request *  gst_v4l2_encoder_request_ref (GstV4l2Request * request);

void              gst_v4l2_encoder_request_unref (GstV4l2Request * request);
void              gst_v4l2_encoder_ro_request_unref (GstV4l2Request * request);


gboolean          gst_v4l2_encoder_request_queue (GstV4l2Request * request,
                                                  guint flags);

gint              gst_v4l2_encoder_request_set_done (GstV4l2Request * request, guint32 * bytesused, guint32 * flags);

void              gst_v4l2_encoder_request_replace_pic_buf (GstV4l2Request * request,
                                                            GstBuffer * pic_buf);

gboolean	  gst_v4l2_codec_vp8_enc_get_qp_range (GstV4l2Encoder * self, guint * qp_min, guint * qp_max);

gboolean	  gst_v4l2_codec_h264_enc_get_qp_range (GstV4l2Encoder * self, guint * qp_min, guint * qp_max);

gboolean      gst_v4l2_encoder_uses_zero_copy (GstV4l2Encoder * self);

G_END_DECLS

#endif /* __GST_V4L2_ENCODER_H__ */
