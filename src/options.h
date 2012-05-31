#define GETOPT_ARG(x,s)  {(x), 1, 0, (s)}
#define GETOPT_FLAG(x,v) {(x), 0, 0, (v)}

#define OPT_HELP "help"
#define OPT_ADDR "addr"
#define OPT_PORT "port"
#define OPT_FILE "file"
#define OPT_SOCK "sock"
#define OPT_FROM "from"
#define OPT_SIZE "size"

#define CMD_SERVE  "serve"
#define CMD_READ   "read"
#define CMD_WRITE  "write"
#define CMD_ACL    "acl"
#define CMD_MIRROR "mirror"
#define CMD_STATUS "status"
#define CMD_HELP   "help"
#define LEN_CMD_MAX 6

#define PATH_LEN_MAX 1024
#define ADDR_LEN_MAX 64


#define IS_CMD(x,c) (strncmp((x),(c),(LEN_CMD_MAX)) == 0)

#define GETOPT_HELP GETOPT_FLAG( OPT_HELP, 'h' )
#define GETOPT_ADDR GETOPT_ARG( OPT_ADDR, 'l' )
#define GETOPT_PORT GETOPT_ARG( OPT_PORT, 'p' )
#define GETOPT_FILE GETOPT_ARG( OPT_FILE, 'f' )
#define GETOPT_SOCK GETOPT_ARG( OPT_SOCK, 's' )
#define GETOPT_FROM GETOPT_ARG( OPT_FROM, 'F' )
#define GETOPT_SIZE GETOPT_ARG( OPT_SIZE, 'S' )

#ifdef DEBUG
#  define OPT_DEBUG "debug"
#  define GETOPT_DEBUG GETOPT_FLAG( OPT_DEBUG, 'd' )
#  define DEBUG_LINE \
  "\t--" OPT_DEBUG ",-d\tOutput debug information.\n"
#else
#  define GETOPT_DEBUG {0}
#  define DEBUG_LINE ""
#endif


static struct option serve_options[] = {
	GETOPT_HELP,
	GETOPT_ADDR,
	GETOPT_PORT,
	GETOPT_FILE,
	GETOPT_SOCK,
	GETOPT_DEBUG,
	{0}
};
static char serve_short_options[] = "hl:p:f:s:";
static char serve_help_text[] =
	"Usage: flexnbd " CMD_SERVE " <options> [<acl address>*]\n\n"
	"Serve FILE from ADDR:PORT, with an optional control socket at SOCK.\n\n"
	"\t--" OPT_HELP ",-h\tThis text.\n"
	"\t--" OPT_ADDR ",-l <ADDR>\tThe address to serve on.\n"
	"\t--" OPT_PORT ",-p <PORT>\tThe port to serve on.\n"
	"\t--" OPT_FILE ",-f <FILE>\tThe file to serve.\n"
	"\t--" OPT_SOCK ",-s <SOCK>\tPath to the control socket to open.\n"
	DEBUG_LINE;

static struct option read_options[] = {
	GETOPT_HELP,
	GETOPT_ADDR,
	GETOPT_PORT,
	GETOPT_FROM,
	GETOPT_SIZE,
	GETOPT_DEBUG,
	{0}
};
static char read_short_options[] = "hl:p:F:S:";
static char read_help_text[] =
	"Usage: flexnbd " CMD_READ " <options>\n\n"
	"Read SIZE bytes from a server at ADDR:PORT to stdout, starting at OFFSET.\n\n"
	"\t--" OPT_HELP ",-h\tThis text.\n"
	"\t--" OPT_ADDR ",-l <ADDR>\tThe address to read from.\n"
	"\t--" OPT_PORT ",-p <PORT>\tThe port to read from.\n"
	"\t--" OPT_FROM ",-F <OFFSET>\tByte offset to read from.\n"
	"\t--" OPT_SIZE ",-S <SIZE>\tBytes to read.\n"
	DEBUG_LINE;


static struct option *write_options = read_options;
static char *write_short_options = read_short_options;
static char write_help_text[] =
	"Usage: flexnbd " CMD_WRITE" <options>\n\n"
	"Write SIZE bytes from stdin to a server at ADDR:PORT, starting at OFFSET.\n\n"
	"\t--" OPT_HELP ",-h\tThis text.\n"
	"\t--" OPT_ADDR ",-l <ADDR>\tThe address to write to.\n"
	"\t--" OPT_PORT ",-p <PORT>\tThe port to write to.\n"
	"\t--" OPT_FROM ",-F <OFFSET>\tByte offset to write from.\n"
	"\t--" OPT_SIZE ",-S <SIZE>\tBytes to write.\n"
	DEBUG_LINE;

struct option acl_options[] = {
	GETOPT_HELP,
	GETOPT_SOCK,
	GETOPT_DEBUG,
	{0}
};
static char acl_short_options[] = "hs:";
static char acl_help_text[] =
	"Usage: flexnbd " CMD_ACL " <options> [<acl address>+]\n\n"
	"Set the access control list for a server with control socket SOCK.\n\n"
	"\t--" OPT_HELP ",-h\tThis text.\n"
	"\t--" OPT_SOCK ",-s <SOCK>\tPath to the control socket.\n"
	DEBUG_LINE;

struct option mirror_options[] = {
	GETOPT_HELP,
	GETOPT_SOCK,
	GETOPT_ADDR,
	GETOPT_PORT,
	GETOPT_DEBUG,
	{0}
};
static char mirror_short_options[] = "hs:l:p:";
static char mirror_help_text[] =
	"Usage: flexnbd " CMD_MIRROR " <options>\n\n"
	"Start mirroring from the server with control socket SOCK to one at ADDR:PORT.\n\n"
	"\t--" OPT_HELP ",-h\tThis text.\n"
	"\t--" OPT_SOCK ",-s <SOCK>\tPath to the control socket.\n"
	"\t--" OPT_ADDR ",-l <ADDR>\tThe address to mirror to.\n"
	"\t--" OPT_PORT ",-p <PORT>\tThe port to mirror to.\n"
	DEBUG_LINE;


struct option status_options[] = {
	GETOPT_HELP,
	GETOPT_SOCK,
	GETOPT_DEBUG,
	{0}
};
static char status_short_options[] = "hs:";
static char status_help_text[] =
	"Usage: flexnbd " CMD_STATUS " <options>\n\n"
	"Get the status for a server with control socket SOCK.\n\n"
	"\t--" OPT_HELP ",-h\tThis text.\n"
	"\t--" OPT_SOCK ",-s <SOCK>\tPath to the control socket.\n"
	DEBUG_LINE;

static char help_help_text[] =
	"Usage: flexnbd <cmd> [cmd options]\n\n"
	"Commands:\n"
	"\tflexnbd serve\n"
	"\tflexnbd read\n"
	"\tflexnbd write\n"
	"\tflexnbd acl\n"
	"\tflexnbd mirror\n"
	"\tflexnbd status\n"
	"\tflexnbd help\n\n"
	"See flexnbd help <cmd> for further info\n";