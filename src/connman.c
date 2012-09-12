/*
 *  Copyright (C) 2012, sleep(5) ltd <http://sleepfive.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2, or (at your option)
 *  any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  Author: Tomas Frydrych <tomas@sleepfive.com>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include <unistd.h>
#include <errno.h>
#include <glib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <readline/readline.h>

#include "main.h"
#include "connman.h"
#include "connman-agent-introspection.h"
#include "mtn-connman.h"
#include "mtn-connman-service.h"

#define PROMPT "wifi> "
/*
 * Most of this comes directly from the MEX networks plugin, including the
 * auxiliary mtn source files.
 */
typedef enum
{
  MTN_CONNMAN_FIELD_IDENTITY      = 0x00000001,
  MTN_CONNMAN_FIELD_USERNAME      = 0x00000010,
  MTN_CONNMAN_FIELD_USERNAME_MASK = 0x00001111,

  MTN_CONNMAN_FIELD_PASSWORD      = 0x00010000,
  MTN_CONNMAN_FIELD_PASSPHRASE    = 0x00100000,
  MTN_CONNMAN_FIELD_PASSWORD_MASK = 0x11110000,
}MtnConnmanFields;

typedef struct
{
  GMainLoop         *loop;
  guint              name_id;
  guint              object_id;

  MtnConnman        *connman;
  MtnConnmanService *service;

  /* Agent stuff */
  GDBusConnection       *connection;
  GDBusNodeInfo         *agent_gir;
  MtnConnmanFields       agent_field_mask;
  GDBusMethodInvocation *agent_input_invocation;

  GHashTable            *services;

  guint agent_submitted  : 1;
  guint agent_registered : 1;
  guint cancelled        : 1;
} ConnmanData;

static void connection_requested (ConnmanData *d, const char *name);

static ConnmanData data = {0,};

static gboolean
submit_passphrase (ConnmanData *d)
{
  GVariant              *input;
  GVariant              *tup;
  GVariantBuilder        builder;
  const char            *pass;
  const char            *name;
  GDBusMethodInvocation *invocation = d->agent_input_invocation;

  if (!invocation)
    {
      g_warning ("Can't submit passpharse without invocation object!");
      return FALSE;
    }

  d->agent_input_invocation = NULL;

  if (d->agent_field_mask & MTN_CONNMAN_FIELD_USERNAME_MASK)
    if (!(name = readline (PROMPT "Enter username: ")) || !*name)
      {
        output (PROMPT "User name is required!\n");
        d->cancelled = TRUE;
        g_dbus_method_invocation_return_dbus_error (invocation,
                                           "net.connman.Agent.Error.Canceled",
                                           "No username");
        g_object_unref (invocation);
        return FALSE;
      }

  if (d->agent_field_mask & MTN_CONNMAN_FIELD_PASSWORD_MASK)
    if (!(pass = readline (PROMPT "Enter passphrase: ")) || !*pass)
      {
        output (PROMPT "Passphrase is required!\n");
        d->cancelled = TRUE;
        g_dbus_method_invocation_return_dbus_error (invocation,
                                           "net.connman.Agent.Error.Canceled",
                                           "No passphrase");
        g_object_unref (invocation);
        return FALSE;
      }

  g_variant_builder_init (&builder, G_VARIANT_TYPE ("(a{sv})"));
  g_variant_builder_open (&builder , G_VARIANT_TYPE ("a{sv}"));

  if (d->agent_field_mask & MTN_CONNMAN_FIELD_PASSWORD)
    {
      g_variant_builder_add (&builder , "{sv}", "Password",
                             g_variant_new_string (pass));
    }
  else if (d->agent_field_mask & MTN_CONNMAN_FIELD_PASSPHRASE)
    {
      g_variant_builder_add (&builder , "{sv}", "Passphrase",
                             g_variant_new_string (pass));
    }
  else if (d->agent_field_mask & MTN_CONNMAN_FIELD_USERNAME)
    {
      g_variant_builder_add (&builder , "{sv}", "Username",
                             g_variant_new_string (name));
    }
  else if (d->agent_field_mask & MTN_CONNMAN_FIELD_IDENTITY)
    {
      g_variant_builder_add (&builder , "{sv}", "Identity",
                             g_variant_new_string (name));
    }

  input = g_variant_builder_end (&builder);
  tup = g_variant_new_tuple (&input, 1);

  g_dbus_method_invocation_return_value (invocation, tup);
  g_object_unref (invocation);

  return TRUE;
}

static void
agent_method_cb (GDBusConnection       *connection,
                 const gchar           *sender,
                 const gchar           *object_path,
                 const gchar           *interface_name,
                 const gchar           *method_name,
                 GVariant              *parameters,
                 GDBusMethodInvocation *invocation,
                 ConnmanData           *d)
{
  if (g_strcmp0 (method_name, "ReportError") == 0)
   {
     const char *object;
     const char *msg;

     g_variant_get (parameters, "(os)", &object, &msg);
     output (PROMPT "Error: '%s'\n", msg);

     g_dbus_method_invocation_return_value (invocation, NULL);
   }
 else if (g_strcmp0 (method_name, "RequestInput") == 0)
   {
     /*
      * Input
      */
     const char   *object;
     GVariantIter *fields;

     /*
      * Output
      */
     const char *field;
     GVariant   *value;

     MtnConnmanFields mask = 0;

     g_variant_get (parameters, "(oa{sv})", &object, &fields);
     while (g_variant_iter_next (fields, "{sv}", &field, &value))
       {
         g_debug ("Got field '%s'", field);

         if (!g_strcmp0 (field, "Passphrase"))
           mask |= MTN_CONNMAN_FIELD_PASSPHRASE;
         else if (!g_strcmp0 (field, "Password"))
           mask |= MTN_CONNMAN_FIELD_PASSWORD;
         else if (!g_strcmp0 (field, "Username"))
           mask |= MTN_CONNMAN_FIELD_USERNAME;
         else if (!g_strcmp0 (field, "Identity"))
           mask |= MTN_CONNMAN_FIELD_IDENTITY;
         else
           g_warning ("Unhandled field '%s'", field);

       }

     d->agent_field_mask = mask;
     g_variant_iter_free (fields);

     if (d->agent_input_invocation)
       {
         output (PROMPT "Warning: RequestInput already in progress!\n");
         g_object_unref (d->agent_input_invocation);
       }

     d->agent_input_invocation = g_object_ref (invocation);

     if (!submit_passphrase (d))
       g_main_loop_quit (d->loop);
   }
 else
   {
     output (PROMPT "Warning method '%s' not implemented\n", method_name);
     g_dbus_method_invocation_return_value (invocation, NULL);
   }
}

static const GDBusInterfaceVTable agent_interface_table =
{
  (GDBusInterfaceMethodCallFunc) agent_method_cb,
  NULL,
  NULL
};

static void
register_agent_cb (GObject *object, GAsyncResult *res, gpointer data)
{
  ConnmanData *d = data;
  GError      *error = NULL;
  GVariant    *var;

  if ((var = g_dbus_proxy_call_finish (G_DBUS_PROXY (object), res, &error)))
    g_variant_unref (var);

  if (error)
    {
      output (PROMPT "error: %s.", error->message);
      g_error_free (error);
      return;
    }

  output ("done.\n");
  d->agent_registered = TRUE;

  if ((var = mtn_connman_get_services (d->connman)))
    {
      GVariantIter *iter, *serv;
      char         *opath;

      g_variant_get (var, "(a(oa{sv}))", &iter);
      while (g_variant_iter_next (iter, "(oa{sv})", &opath, &serv))
        {
          char       *key;
          GVariant   *value;
          GHashTable *props;
          gboolean    is_wifi = FALSE;
          const char *name = NULL;

          props = g_hash_table_new_full (g_str_hash, g_str_equal,
                                         g_free,
                                         (GDestroyNotify)g_variant_unref);

          while (g_variant_iter_next (serv, "{sv}", &key, &value))
            {
              g_hash_table_insert (props, key, value);

              if (!g_strcmp0 (key, "Type") &&
                  !g_strcmp0 ("wifi", g_variant_get_string (value, NULL)))
                is_wifi = TRUE;

              if (!g_strcmp0 (key, "Name"))
                name = g_variant_get_string (value, NULL);
            }

          if (!is_wifi || !name)
            g_hash_table_destroy (props);
          else
            {
              g_hash_table_insert (props, g_strdup ("DbusPath"), opath);
              g_hash_table_insert (d->services, (gpointer)name, props);
            }

          g_variant_iter_free (serv);
        }

      g_variant_iter_free (iter);
    }

  if (d->services)
    {
      int         i;
      GList      *keys, *l;
      char       *sel;
      const char *key;
      gboolean    quit = FALSE;

      output (PROMPT "Available networks:\n" PROMPT "\n");

      keys = g_hash_table_get_keys (d->services);
      for (i = 0, l = keys; l; l = l->next, i++)
        {
          output (PROMPT "    %d: %s\n", i+1, (char*)l->data);
        }

      output (PROMPT "\n" PROMPT "Select wifi [1-%d]:\n", i);

      if ((sel = readline (PROMPT "? ")))
        {
          if ((i = strtol (sel, NULL, 10)) < 1)
            quit = TRUE;
          else
            {
              key = g_list_nth_data (keys, i-1);
              connection_requested (d, key);
            }
        }
      else
        quit = TRUE;

      g_list_free (keys);
      free (sel);

      if (quit)
        g_main_loop_quit (d->loop);
    }
}

static void
agent_bus_acquired (GObject      *source_object,
                    GAsyncResult *result,
                    gpointer      data)
{
  ConnmanData *d = data;
  GError      *error = NULL;

  d->connection = g_bus_get_finish (result, &error);

  if (error)
    {
      output (PROMPT "error: %s\n", error->message);
      g_error_free (error);
      return;
    }
  else
    output ("done.\nwifi> Registering agent ... ");

  d->object_id =
    g_dbus_connection_register_object (d->connection,
                                       "/org/GuacamayoProject/ConnmanAgent",
                                       d->agent_gir->interfaces[0],
                                       &agent_interface_table,
                                       d,
                                       NULL,
                                       &error);

  if (error)
    {
      output ("failed: %s\n", error->message);
      g_error_free (error);
    }
  else if (d->connman && !d->agent_submitted)
    {
      GVariant *o =
        g_variant_new ("(o)", "/org/GuacamayoProject/ConnmanAgent");

      g_dbus_proxy_call (G_DBUS_PROXY (d->connman), "RegisterAgent", o,
                         G_DBUS_CALL_FLAGS_NONE, 120000, NULL,
                         register_agent_cb, d);
      d->agent_submitted = TRUE;
    }
}

static void
register_agent (ConnmanData *d)
{
  GError *error = NULL;

  d->name_id = g_bus_own_name (G_BUS_TYPE_SYSTEM,
                               "org.guacamayo-project.ConnmanAgent",
                               G_BUS_NAME_OWNER_FLAGS_NONE,
                               NULL,
                               NULL,
                               NULL,
                               NULL,
                               NULL);

  if (!(d->agent_gir =
        g_dbus_node_info_new_for_xml (connman_agent_introspection, &error)))
    {
      g_warning ("Error %s", error->message);
      g_clear_error (&error);
    }

  output (PROMPT "Connecting to DBus ... ");
  g_bus_get (G_BUS_TYPE_SYSTEM, NULL, agent_bus_acquired, d);
}

static void
connman_new_cb (GObject *object, GAsyncResult *res, gpointer data)
{
  ConnmanData *d = data;
  GError      *error = NULL;
  GVariant    *var;

  d->connman = mtn_connman_new_finish (res, &error);
  if (!d->connman)
    {
      output (PROMPT "Connman proxy: %s\n", error->message);
      g_error_free (error);
      return;
    }

  /*
   * Only initiate this here, so we do not get output messages mixed up
   * on the console due to the async nature.
   */
  register_agent (d);
}

static void
service_state_changed_cb (MtnConnmanService *service,
                          GVariant          *state,
                          ConnmanData       *d)
{
  const char *val;

  g_return_if_fail (state);

  val = g_variant_get_string (state, NULL);

  if (!g_strcmp0 (val, "online"))
    {
      output (PROMPT "Online.\n");
    }
  else if (!g_strcmp0 (val, "ready"))
    {
      output (PROMPT "Connected.\n");
    }
  else if (!g_strcmp0 (val, "failure"))
    {
      output (PROMPT "Connection failed.\n");
    }
  else if (!g_strcmp0 (val, "disconnect"))
    {
      output (PROMPT "Disconnected\n");
    }

  g_main_loop_quit (d->loop);
}

static gboolean
is_connman_error (GError *error, char *name)
{
  char *remote_error;

  if (!g_dbus_error_is_remote_error (error))
    return FALSE;

  if (!(remote_error = g_dbus_error_get_remote_error (error)))
    return FALSE;

  if (g_str_has_prefix (remote_error, "net.connman.Error."))
    {
      if (!name || g_str_has_suffix (remote_error, name))
        return TRUE;
    }

  return FALSE;
}

static void
connect_cb (GObject *object, GAsyncResult *res, gpointer data)
{
  ConnmanData *d = data;
  GVariant    *var;
  GError      *error = NULL;

  if ((var = g_dbus_proxy_call_finish (G_DBUS_PROXY (object), res, &error)))
    g_variant_unref (var);

  if (error)
    {
      if (is_connman_error (error, "AlreadyConnected"))
        {
          output (PROMPT "Already connected.\n");
        }
      else if (is_connman_error (error, "InProgress"))
        {
          output (PROMPT "Connection in progress.\n");
        }
      else if (is_connman_error (error, "InvalidArguments"))
        {
          if (!d->cancelled)
            output (PROMPT "Connection failed: %s.\n", error->message);
        }
      else
        {
          output (PROMPT "Connection failed: %s.\n", error->message);
        }

      g_error_free (error);
    }
}

static void
connman_service_new_cb (GObject *object, GAsyncResult *res, gpointer data)
{
  ConnmanData *d = data;
  GError      *error = NULL;

  if (!(d->service = mtn_connman_service_new_finish (res, &error)))
    {
      output (PROMPT "Failed to connect: %s\n", error->message);
      g_error_free (error);

      return;
    }

  g_signal_connect (d->service, "property-changed::State",
                    G_CALLBACK (service_state_changed_cb),
                    d);

  g_dbus_proxy_call (G_DBUS_PROXY (d->service), "Connect", NULL,
                     G_DBUS_CALL_FLAGS_NONE, 120000, NULL,
                     connect_cb, d);

  output (PROMPT "Connecting ... \n");
}

static void
connection_requested (ConnmanData *d, const char *name)
{
  GHashTable *props;
  const char *object_path;

  if (!(props = g_hash_table_lookup (d->services, name)))
    {
      g_warning ("Unknown service '%s'", name);
      return;
    }


  if (!(object_path = g_hash_table_lookup (props, "DbusPath")))
    {
      g_warning ("No object path in connection request");
      return;
    }

  if (d->service)
    {
      g_signal_handlers_disconnect_by_data (d->service, d);
      g_object_unref (d->service);
      d->service = NULL;
    }

  mtn_connman_service_new (object_path, NULL, connman_service_new_cb, d);
}

static void
connman_init (ConnmanData *d)
{
  mtn_connman_new (NULL, connman_new_cb, d);
};

gboolean
setup_wifi (char *line)
{
  gboolean retval = TRUE;
  ConnmanData *d = g_slice_new0 (ConnmanData);
  GMainLoop   *loop;
  GVariant    *o;

  /*
   * The services hash is keyed by the service name (i.e., ssid) and holds
   * hash table of the service properties. The actual key value is inside the
   * property hash, so we do not free it when destroying the hash.
   */
  d->services = g_hash_table_new_full (g_str_hash, g_str_equal,
                                       NULL,
                                       (GDestroyNotify)g_hash_table_destroy);

  d->loop = loop = g_main_loop_new (NULL, FALSE);

  connman_init (d);

  g_main_loop_run (loop);

  o = g_variant_new ("(o)", "/org/GuacamayoProject/ConnmanAgent");

  g_dbus_proxy_call_sync (G_DBUS_PROXY (d->connman), "UnregisterAgent", o,
                          G_DBUS_CALL_FLAGS_NONE, 120000, NULL, NULL);

  g_dbus_connection_unregister_object (d->connection, d->object_id);
  g_bus_unown_name (d->name_id);

  g_object_unref (d->connman);

  if (d->service)
    g_object_unref (d->service);

  g_hash_table_destroy (d->services);

  while (g_main_context_pending (g_main_loop_get_context (d->loop)))
    g_main_context_iteration (g_main_loop_get_context (d->loop), FALSE);

  g_main_loop_unref (d->loop);

  g_slice_free (ConnmanData, d);

  return retval;
}

