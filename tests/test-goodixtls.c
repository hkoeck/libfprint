/* SPDX-License-Identifier: LGPL-2.1-or-later */

/* Exercise private command state without a USB device or a TLS peer. */
#include "drivers/goodixtls/goodix.c"

typedef struct
{
  FpiDeviceGoodixTls parent;
} TestGoodixDevice;

typedef FpiDeviceGoodixTlsClass TestGoodixDeviceClass;

GType test_goodix_device_get_type (void);
G_DEFINE_TYPE (TestGoodixDevice, test_goodix_device, FPI_TYPE_DEVICE_GOODIXTLS)

static void
test_goodix_device_class_init (TestGoodixDeviceClass *klass)
{
  FpDeviceClass *device_class = FP_DEVICE_CLASS (klass);

  device_class->type = FP_DEVICE_TYPE_VIRTUAL;
  device_class->features = FP_DEVICE_FEATURE_VERIFY;
  device_class->temp_hot_seconds = -1;
}

static void
test_goodix_device_init (TestGoodixDevice *self)
{
}

static void
command_cancelled (FpDevice *dev, guint8 *data, guint16 length,
                   gpointer user_data, GError *error)
{
  guint *calls = user_data;
  FpiDeviceGoodixTlsPrivate *priv = fpi_device_goodixtls_get_instance_private (
      FPI_DEVICE_GOODIXTLS (dev));

  (*calls)++;
  g_assert_error (error, G_IO_ERROR, G_IO_ERROR_CANCELLED);
  g_assert_null (data);
  g_assert_cmpuint (length, ==, 0);
  g_assert_false (priv->ack);
  g_assert_false (priv->reply);
  g_assert_null (priv->callback);
  g_assert_null (priv->user_data);
  g_clear_error (&error);
}

static void
test_cancel_command (gconstpointer user_data)
{
  g_autoptr(FpDevice) dev = g_object_new (test_goodix_device_get_type (), NULL);
  FpiDeviceGoodixTlsPrivate *priv = fpi_device_goodixtls_get_instance_private (
      FPI_DEVICE_GOODIXTLS (dev));
  guint calls = 0;
  guint state = GPOINTER_TO_UINT (user_data);

  priv->ack = state == 1;
  priv->reply = state == 2;
  priv->callback = command_cancelled;
  priv->user_data = &calls;

  goodix_cancel_pending_command (dev);
  g_assert_cmpuint (calls, ==, state ? 1 : 0);
  /* Cancelling an already completed command must not invoke its callback again. */
  goodix_cancel_pending_command (dev);
  g_assert_cmpuint (calls, ==, state ? 1 : 0);
}

static void
tls_failed (FpDevice *dev, gpointer user_data, GError *error)
{
  guint *calls = user_data;
  FpiDeviceGoodixTlsPrivate *priv = fpi_device_goodixtls_get_instance_private (
      FPI_DEVICE_GOODIXTLS (dev));

  (*calls)++;
  g_assert_null (priv->tls_ready_callback);
  g_assert_error (error, G_IO_ERROR, G_IO_ERROR_CANCELLED);
  g_clear_error (&error);
}

static void
test_tls_failure (void)
{
  g_autoptr(FpDevice) dev = g_object_new (test_goodix_device_get_type (), NULL);
  FpiDeviceGoodixTlsPrivate *priv = fpi_device_goodixtls_get_instance_private (
      FPI_DEVICE_GOODIXTLS (dev));
  guint calls = 0;

  priv->tls_ready_callback = goodix_make_cb_info (G_CALLBACK (tls_failed), &calls);
  g_test_expect_message ("libfprint-goodixtls", G_LOG_LEVEL_CRITICAL,
                         "*TLS handshake failed*");
  tls_handshake_done (NULL, dev,
                     g_error_new_literal (G_IO_ERROR, G_IO_ERROR_CANCELLED,
                                          "Cancelled during TLS handshake"));
  g_test_assert_expected_messages ();
  g_assert_cmpuint (calls, ==, 1);
}

typedef struct
{
  guint   calls;
  GError *error;
} TlsReadyResult;

static void
tls_ready (FpDevice *dev, gpointer user_data, GError *error)
{
  TlsReadyResult *result = user_data;

  result->calls++;
  g_clear_error (&result->error);
  result->error = error;
}

static void
test_tls_established (gconstpointer user_data)
{
  g_autoptr(FpDevice) dev = g_object_new (test_goodix_device_get_type (), NULL);
  FpiDeviceGoodixTlsPrivate *priv = fpi_device_goodixtls_get_instance_private (
      FPI_DEVICE_GOODIXTLS (dev));
  gboolean cancel = GPOINTER_TO_INT (user_data);
  TlsReadyResult result = { 0 };

  /* State left by goodix_send_tls_successfully_established(): waiting for an
   * ACK that never comes, so it normally completes through its 10 ms timeout. */
  priv->tls_ready_callback = goodix_make_cb_info (G_CALLBACK (tls_ready), &result);
  priv->ack = TRUE;
  priv->callback = goodix_receive_none;
  priv->user_data = goodix_make_cb_info (G_CALLBACK (on_tls_successfully_established), NULL);

  if (cancel)
    goodix_cancel_pending_command (dev);
  else
    goodix_receive_timeout_cb (dev, NULL);

  g_assert_cmpuint (result.calls, ==, 1);
  g_assert_null (priv->tls_ready_callback);
  if (cancel)
    g_assert_error (result.error, G_IO_ERROR, G_IO_ERROR_CANCELLED);
  else
    g_assert_no_error (result.error);
  g_clear_error (&result.error);
}

int
main (int argc, char **argv)
{
  g_test_init (&argc, &argv, NULL);
  g_test_add_data_func ("/goodixtls/cancel/idle", GUINT_TO_POINTER (0), test_cancel_command);
  g_test_add_data_func ("/goodixtls/cancel/ack", GUINT_TO_POINTER (1), test_cancel_command);
  g_test_add_data_func ("/goodixtls/cancel/reply", GUINT_TO_POINTER (2), test_cancel_command);
  g_test_add_func ("/goodixtls/tls/failure", test_tls_failure);
  g_test_add_data_func ("/goodixtls/tls/established/timeout", GINT_TO_POINTER (FALSE), test_tls_established);
  g_test_add_data_func ("/goodixtls/tls/established/cancel", GINT_TO_POINTER (TRUE), test_tls_established);
  return g_test_run ();
}
