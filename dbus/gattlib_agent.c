
#include "gattlib_internal.h"

static gchar agent_name[] = "/com/dp/smartammbluezble/agent";
static GDBusConnection *conn = NULL;

static const GDBusArgInfo arg1_authorizeservice = {
	-1, "device", "o", NULL
};

static const GDBusArgInfo arg2_authorizeservice = {
	-1, "uuid", "s", NULL
};

static const GDBusArgInfo arg1_requestauthorization = {
	-1, "device", "o", NULL
};

static const GDBusArgInfo arg1_requestconfirmation = {
	-1, "device", "o", NULL
};

static const GDBusArgInfo arg2_requestconfirmation = {
	-1, "passkey", "u", NULL
};

static const GDBusArgInfo arg1_displaypasskey = {
	-1, "device", "o", NULL
};

static const GDBusArgInfo arg2_displaypasskey = {
	-1, "passkey", "u", NULL
};

static const GDBusArgInfo arg3_displaypasskey = {
	-1, "entered", "q", NULL
};

static const GDBusArgInfo arg1_requestpasskey = {
	-1, "device", "o", NULL
};
static const GDBusArgInfo arg2_requestpasskey = {
	-1, "key", "u", NULL
};
static const GDBusArgInfo arg1_displaypincode = {
	-1, "object", "o", NULL
};
static const GDBusArgInfo arg2_displaypincode = {
	-1, "pincode", "s", NULL
};

static const GDBusArgInfo arg1_requestpincode = {
	-1, "device", "o", NULL
};
static const GDBusArgInfo arg2_requestpincode = {
	-1, "key", "s", NULL
};

static const GDBusArgInfo *in_AuthorizeService[] = {
	&arg1_authorizeservice,
	&arg2_authorizeservice,
	NULL
};
static const GDBusArgInfo *in_RequestAuthorization[] = {
	&arg1_requestauthorization,
	NULL
};
static const GDBusArgInfo *in_RequestConfirmation[] = {
	&arg1_requestconfirmation,
	&arg2_requestconfirmation,
	NULL
};
static const GDBusArgInfo *in_DisplayPasskey[] = {
	&arg1_displaypasskey,
	&arg2_displaypasskey,
	&arg3_displaypasskey,
	NULL
};

static const GDBusArgInfo *in_RequestPasskey[] = {
	&arg1_requestpasskey,
	NULL
};
static const GDBusArgInfo *out_RequestPasskey[] = {
	&arg2_requestpasskey,
	NULL
};

static const GDBusArgInfo *in_DisplayPinCode[] = {
	&arg1_displaypincode,
	&arg2_displaypincode,
	NULL
};
static const GDBusArgInfo *in_RequestPincode[] = {
	&arg1_requestpincode,
	NULL
};
static const GDBusArgInfo *out_RequestPincode[] = {
	&arg2_requestpincode,
	NULL
};
static const GDBusMethodInfo agent_iface_endpoint_AuthorizeService = {
	-1, "AuthorizeService",
	(GDBusArgInfo **)in_AuthorizeService,
	NULL,
	NULL,
};
static const GDBusMethodInfo agent_iface_endpoint_Cancel = {
	-1, "Cancel",
	NULL,
	NULL,
	NULL,
};
static const GDBusMethodInfo agent_iface_endpoint_RequestAuthorization = {
	-1, "RequestAuthorization",
	(GDBusArgInfo **)in_RequestAuthorization,
	NULL,
	NULL,
};
static const GDBusMethodInfo agent_iface_endpoint_RequestConfirmation = {
	-1, "RequestConfirmation",
	(GDBusArgInfo **)in_RequestConfirmation,
	NULL,
	NULL,
};
static const GDBusMethodInfo agent_iface_endpoint_DisplayPasskey = {
	-1, "DisplayPaskey",
	(GDBusArgInfo **)in_DisplayPasskey,
	NULL,
	NULL,
};
static const GDBusMethodInfo agent_iface_endpoint_RequestPasskey = {
	-1, "RequestPasskey",
	(GDBusArgInfo **)in_RequestPasskey,
	(GDBusArgInfo **)out_RequestPasskey,
	NULL,
};
static const GDBusMethodInfo agent_iface_endpoint_DisplayPinCode = {
	-1, "DisplayPinCode",
	(GDBusArgInfo **)in_DisplayPinCode,
	NULL,
	NULL,
};
static const GDBusMethodInfo agent_iface_endpoint_RequestPinCode = {
	-1, "RequestPasskey",
	(GDBusArgInfo **)in_RequestPincode,
	(GDBusArgInfo **)out_RequestPincode,
	NULL,
};
static const GDBusMethodInfo agent_iface_endpoint_Release = {
	-1, "Release",
	NULL,
	NULL,
	NULL,
};
static const GDBusMethodInfo *agent_iface_endpoint_methods[] = {
	&agent_iface_endpoint_AuthorizeService,
	&agent_iface_endpoint_Cancel,
	&agent_iface_endpoint_RequestAuthorization,
	&agent_iface_endpoint_RequestConfirmation,
	&agent_iface_endpoint_DisplayPasskey,
	&agent_iface_endpoint_RequestPasskey,
	&agent_iface_endpoint_DisplayPinCode,
	&agent_iface_endpoint_RequestPinCode,
	&agent_iface_endpoint_Release,
};

const GDBusInterfaceInfo agent_iface_endpoint = {
	-1, "org.bluez.Agent1",
	(GDBusMethodInfo **)agent_iface_endpoint_methods,
	NULL,
	NULL,
	NULL,
};

/* dummy agent - don't reply to commands */
static void agent_endpoint_method_call(GDBusConnection *conn, const gchar *sender,
		const gchar *path, const gchar *interface, const gchar *method, GVariant *params,
		GDBusMethodInvocation *invocation, void *userdata) {
	fprintf(stderr, "Agent called with command: %s\n",method);
	g_object_unref(invocation);
	return ;
}

static const GDBusInterfaceVTable endpoint_vtable = {
	.method_call = agent_endpoint_method_call,
};
static void endpoint_free(gpointer data) {
	(void)data;
}

static void bus_acquired_handler(GDBusConnection *connection, const gchar *name, gpointer user_data)
{
	fprintf(stderr, "Bus acquired: %s\n", name);
	conn = connection;

	GError *error = NULL;
	guint rid = 0;
	if ((rid = g_dbus_connection_register_object(conn,agent_name ,
				(GDBusInterfaceInfo *)&agent_iface_endpoint, &endpoint_vtable,
				NULL, endpoint_free, &error)) == 0){

		g_error_free(error);
	}
}

static void name_acquired_handler(GDBusConnection *connection, const gchar *name, gpointer user_data)
{
	fprintf(stderr, "Name acquired: %s\n", name);
}

static void name_lost_handler(GDBusConnection *connection, const gchar *name, gpointer user_data)
{
	fprintf(stderr, "Name lost: %s\n", name);
}

void gattlib_register_default_agent(void)
{
	GError *error = NULL;
	OrgBluezAgentManager1 *agentmgr = org_bluez_agent_manager1_proxy_new_for_bus_sync(
			G_BUS_TYPE_SYSTEM,
			G_DBUS_OBJECT_MANAGER_CLIENT_FLAGS_NONE,
			"org.bluez",
			"/org/bluez",
			NULL,
			&error);
	if(error) {
		fprintf(stderr, "Error creating agent mgr proxy: %s\n", error->message);
		g_error_free(error);
		return;
	}

	error = NULL;
	org_bluez_agent_manager1_call_register_agent_sync(
			agentmgr,
			agent_name,
			"KeyboardDisplay",
			NULL,
			&error);
	if(error) {
		fprintf(stderr, "Error registering agent: %s\n", error->message);
		g_error_free(error);
		return;
	}

	error = NULL;
	org_bluez_agent_manager1_call_request_default_agent_sync(
			agentmgr,
			agent_name,
			NULL,
			&error);
	if(error) {
		fprintf(stderr, "Error registering default agent: %s\n", error->message);
		g_error_free(error);
		return;
	}

	g_bus_own_name(G_BUS_TYPE_SYSTEM, "com.dp.smartammbluezble.agent", G_BUS_NAME_OWNER_FLAGS_NONE, bus_acquired_handler, name_acquired_handler, name_lost_handler, NULL, NULL);
}
