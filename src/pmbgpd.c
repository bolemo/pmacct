/*  
    pmacct (Promiscuous mode IP Accounting package)
    pmacct is Copyright (C) 2003-2023 by Paolo Lucente
*/

/*
    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
*/

/* includes */
#include "pmacct.h"
#include "pmacct-build.h"
#include "plugin_hooks.h"
#include "bgp/bgp.h"
#include "bgp/bgp_lg.h"
#include "pmbgpd.h"
#include "pretag_handlers.h"
#include "pmacct-data.h"
#include "pkt_handlers.h"
#include "ip_flow.h"
#include "net_aggr.h"
#include "thread_pool.h"
#ifdef WITH_REDIS
#include "ha.h"
#endif

/* Functions */
void usage_daemon(char *prog_name)
{
  printf("%s %s (%s)\n", PMBGPD_USAGE_HEADER, PMACCT_VERSION, PMACCT_BUILD);
  printf("Usage: %s [ -D | -d ] [ -L IP address ] [ -l port ] ]\n", prog_name);
  printf("       %s [ -f config_file ]\n", prog_name);
  printf("       %s [ -h ]\n", prog_name);
  printf("\nGeneral options:\n");
  printf("  -h  \tShow this page\n");
  printf("  -V  \tShow version and compile-time options and exit\n");
  printf("  -L  \tBind to the specified IP address\n");
  printf("  -l  \tListen on the specified TCP port\n");
  printf("  -f  \tLoad configuration from the specified file\n");
  printf("  -D  \tDaemonize\n");
  printf("  -d  \tEnable debug\n");
  printf("  -S  \t[ auth | mail | daemon | kern | user | local[0-7] ] \n\tLog to the specified syslog facility\n");
  printf("  -F  \tWrite Core Process PID into the specified file\n");
  printf("  -o  \tOutput file to log real-time BGP messages\n");
  printf("  -O  \tOutput file to dump generated RIBs at regular time intervals\n");
  printf("  -i  \tInterval, in secs, to write to the dump output file (supplied by -O)\n");
  printf("  -g  \tEnable the Looking Glass server\n");
  printf("  -m  \tLoad a BGP xconnects map from the specified file\n");
  printf("\n");
  printf("For examples, see:\n");
  printf("  https://github.com/pmacct/pmacct/blob/master/QUICKSTART or\n");
  printf("  https://github.com/pmacct/pmacct/wiki\n");
  printf("\n");
  printf("For suggestions, critics, bugs, contact me: %s.\n", MANTAINER);
}

int main(int argc,char **argv, char **envp)
{
  struct plugins_list_entry *list;
  char config_file[SRVBUFLEN];
  int logf;

  /* getopt() stuff */
  extern char *optarg;
  extern int optind, opterr, optopt;
  int errflag, cp;

#ifdef WITH_REDIS
  struct p_redis_host redis_host, redis_ha_host;
#endif

#if defined HAVE_MALLOPT
  mallopt(M_CHECK_ACTION, 0);
#endif

  umask(077);

  memset(cfg_cmdline, 0, sizeof(cfg_cmdline));
  memset(&config, 0, sizeof(struct configuration));
  memset(&config_file, 0, sizeof(config_file));
  memset(empty_mem_area_256b, 0, sizeof(empty_mem_area_256b));

  log_notifications_init(&log_notifications);
  config.acct_type = ACCT_PMBGP;
  config.progname = pmbgpd_globstr;

  find_id_func = NULL;
  plugins_list = NULL;
  errflag = 0;
  rows = 0;

  /* needed for pre_tag_map support */
  PvhdrSz = sizeof(struct pkt_vlen_hdr_primitives);
  PmLabelTSz = sizeof(pm_label_t);
  PtLabelTSz = sizeof(pt_label_t);

  /* getting commandline values */
  while (!errflag && ((cp = getopt(argc, argv, ARGS_PMBGPD)) != -1)) {
    cfg_cmdline[rows] = malloc(SRVBUFLEN);
    switch (cp) {
    case 'L':
      strlcpy(cfg_cmdline[rows], "bgp_daemon_ip: ", SRVBUFLEN);
      strncat(cfg_cmdline[rows], optarg, CFG_LINE_LEN(cfg_cmdline[rows]));
      rows++;
      break;
    case 'l':
      strlcpy(cfg_cmdline[rows], "bgp_daemon_port: ", SRVBUFLEN);
      strncat(cfg_cmdline[rows], optarg, CFG_LINE_LEN(cfg_cmdline[rows]));
      rows++;
      break;
    case 'D':
      strlcpy(cfg_cmdline[rows], "daemonize: true", SRVBUFLEN);
      rows++;
      break;
    case 'd':
      debug = TRUE;
      strlcpy(cfg_cmdline[rows], "debug: true", SRVBUFLEN);
      rows++;
      break;
    case 'f':
      strlcpy(config_file, optarg, sizeof(config_file));
      break;
    case 'F':
      strlcpy(cfg_cmdline[rows], "pidfile: ", SRVBUFLEN);
      strncat(cfg_cmdline[rows], optarg, CFG_LINE_LEN(cfg_cmdline[rows]));
      rows++;
      break;
    case 'S':
      strlcpy(cfg_cmdline[rows], "syslog: ", SRVBUFLEN);
      strncat(cfg_cmdline[rows], optarg, CFG_LINE_LEN(cfg_cmdline[rows]));
      rows++;
      break;
    case 'o':
      strlcpy(cfg_cmdline[rows], "bgp_daemon_msglog_file: ", SRVBUFLEN);
      strncat(cfg_cmdline[rows], optarg, CFG_LINE_LEN(cfg_cmdline[rows]));
      rows++;
      break;
    case 'O':
      strlcpy(cfg_cmdline[rows], "bgp_table_dump_file: ", SRVBUFLEN);
      strncat(cfg_cmdline[rows], optarg, CFG_LINE_LEN(cfg_cmdline[rows]));
      rows++;
      break;
    case 'i':
      strlcpy(cfg_cmdline[rows], "bgp_table_dump_refresh_time: ", SRVBUFLEN);
      strncat(cfg_cmdline[rows], optarg, CFG_LINE_LEN(cfg_cmdline[rows]));
      rows++;
      break;
    case 'g':
      strlcpy(cfg_cmdline[rows], "bgp_daemon_lg: true", SRVBUFLEN);
      rows++;
      break;
    case 'm':
      strlcpy(cfg_cmdline[rows], "bgp_daemon_xconnect_map: ", SRVBUFLEN);
      strncat(cfg_cmdline[rows], optarg, CFG_LINE_LEN(cfg_cmdline[rows]));
      rows++;
      break;
    case 'h':
      usage_daemon(argv[0]);
      exit(0);
      break;
    case 'V':
      version_daemon(config.acct_type, PMBGPD_USAGE_HEADER);
      exit(0);
      break;
    default:
      usage_daemon(argv[0]);
      exit(1);
      break;
    }
  }

  /* post-checks and resolving conflicts */
  if (strlen(config_file)) {
    if (parse_configuration_file(config_file) != SUCCESS)
      exit(1);
  }
  else {
    if (parse_configuration_file(NULL) != SUCCESS)
      exit(1);
  }

  list = plugins_list;
  while (list) {
    list->cfg.acct_type = ACCT_PMBGP;
    list->cfg.progname = pmbgpd_globstr;
    set_default_preferences(&list->cfg);
    if (!strcmp(list->type.string, "core")) {
      memcpy(&config, &list->cfg, sizeof(struct configuration));
      config.name = list->name;
      config.type = list->type.string;
    }
    list = list->next;
  }

  if (config.files_umask) umask(config.files_umask);

  initsetproctitle(argc, argv, envp);

  if (config.syslog) {
    logf = parse_log_facility(config.syslog);
    if (logf == ERR) {
      config.syslog = NULL;
      printf("WARN ( %s/core ): specified syslog facility is not supported. Logging to standard error (stderr).\n", config.name);
    }
    else openlog(NULL, LOG_PID, logf);
    Log(LOG_INFO, "INFO ( %s/core ): Start logging ...\n", config.name);
  }

  if (config.logfile) {
    config.logfile_fd = open_output_file(config.logfile, "a", FALSE);
    while (list) {
      list->cfg.logfile_fd = config.logfile_fd ;
      list = list->next;
    }
  }

  if (config.daemon) {
    if (!config.syslog && !config.logfile) {
      if (debug || config.debug) {
	printf("WARN ( %s/core ): debug is enabled; forking in background. Logging to standard error (stderr) will get lost.\n", config.name);
      }
    }

    daemonize();
  }

  if (config.proc_priority) {
    int ret;

    ret = setpriority(PRIO_PROCESS, 0, config.proc_priority);
    if (ret) Log(LOG_WARNING, "WARN ( %s/core ): proc_priority failed (errno: %d)\n", config.name, errno);
    else Log(LOG_INFO, "INFO ( %s/core ): proc_priority set to %d\n", config.name, getpriority(PRIO_PROCESS, 0));
  }

  Log(LOG_INFO, "INFO ( %s/core ): %s %s (%s)\n", config.name, PMBGPD_USAGE_HEADER, PMACCT_VERSION, PMACCT_BUILD);
  Log(LOG_INFO, "INFO ( %s/core ): %s\n", config.name, PMACCT_COMPILE_ARGS);

  if (strlen(config_file)) {
    char canonical_path[PATH_MAX], *canonical_path_ptr;

    canonical_path_ptr = realpath(config_file, canonical_path);
    if (canonical_path_ptr) Log(LOG_INFO, "INFO ( %s/core ): Reading configuration file '%s'.\n", config.name, canonical_path);
  }
  else Log(LOG_INFO, "INFO ( %s/core ): Reading configuration from cmdline.\n", config.name);

  pm_setproctitle("%s [%s]", "Core Process", config.proc_name);
  if (config.pidfile) write_pid_file(config.pidfile);

  /* signal handling we want to inherit to plugins (when not re-defined elsewhere) */
  memset(&sighandler_action, 0, sizeof(sighandler_action)); /* To ensure the struct holds no garbage values */
  sigemptyset(&sighandler_action.sa_mask);  /* Within a signal handler all the signals are enabled */
  sighandler_action.sa_flags = SA_RESTART;  /* To enable re-entering a system call afer done with signal handling */

  /* handles reopening of syslog channel */
  sighandler_action.sa_handler = reload;
  sigaction(SIGHUP, &sighandler_action, NULL);

  /* logs various statistics via Log() calls */
  sighandler_action.sa_handler = SIG_IGN;
  sigaction(SIGUSR1, &sighandler_action, NULL);

  /* sets to true the reload_maps flag */
  sighandler_action.sa_handler = reload_maps;
  sigaction(SIGUSR2, &sighandler_action, NULL);

  /* we want to exit gracefully when a pipe is broken */
  sighandler_action.sa_handler = SIG_IGN;
  sigaction(SIGPIPE, &sighandler_action, NULL);

  sighandler_action.sa_handler = PM_sigint_handler;
  sigaction(SIGINT, &sighandler_action, NULL);

  sighandler_action.sa_handler = PM_sigint_handler;
  sigaction(SIGTERM, &sighandler_action, NULL);

  sighandler_action.sa_handler = SIG_IGN;
  sigaction(SIGCHLD, &sighandler_action, NULL);

  sighandler_action.sa_handler = PM_sigalrm_noop_handler;
  sigaction(SIGALRM, &sighandler_action, NULL);

#ifdef WITH_REDIS
  /* Signals for BMP-BGP-HA feature */
  if (config.bgp_bmp_daemon_ha) {
    /* reset the local timestamp of the collector (SIGNAL=34)*/
    sighandler_action.sa_handler = bmp_bgp_ha_regenerate_timestamp;
    sigaction(SIGRTMIN, &sighandler_action, NULL);

    /* set collector to forced active mode (SIGNAL=35)*/
    sighandler_action.sa_handler = bmp_bgp_ha_set_to_active;
    sigaction(SIGRTMIN + 1, &sighandler_action, NULL);

    /* set collector to forced stand-by mode (SIGNAL=36)*/
    sighandler_action.sa_handler = bmp_bgp_ha_set_to_standby;
    sigaction(SIGRTMIN + 2, &sighandler_action, NULL);

    /* set collector back to timestamp-based (i.e. non-forced) mode (SIGNAL=37)*/
    sighandler_action.sa_handler = bmp_bgp_ha_set_to_normal;
    sigaction(SIGRTMIN + 3, &sighandler_action, NULL);
  }
#endif

  if (!config.bgp_daemon) config.bgp_daemon = BGP_DAEMON_ONLINE;
  if (!config.bgp_daemon_port) config.bgp_daemon_port = BGP_TCP_PORT;

#if defined WITH_ZMQ
  if (config.bgp_lg) bgp_lg_wrapper();
#else
  if (config.bgp_lg) {
    Log(LOG_ERR, "ERROR ( %s/core/lg ): 'bgp_daemon_lg' requires --enable-zmq. Exiting.\n", config.name);
    exit_gracefully(1);
  }
#endif

#ifdef WITH_REDIS
  /* Kicking off redis-reladed thread(s) */
  if (config.redis_host) {
    char log_id[SHORTBUFLEN];

    snprintf(log_id, sizeof(log_id), "%s/%s", config.name, config.type);

    if (config.bgp_bmp_daemon_ha) {
      /* If BMP-BGP-HA feature is enabled, redirect redis thread */
      p_redis_init(&redis_ha_host, log_id, p_redis_thread_bmp_bgp_ha_handler);
    }

    p_redis_init(&redis_host, log_id, p_redis_thread_produce_common_core_handler);
  }
#endif

#ifdef WITH_REDIS
  if (config.bgp_bmp_daemon_ha) {
    /* Kicking off BMP-BGP-HA feature (BMP-BGP Daemon High Availability) */
    bmp_bgp_ha_main();
  }
#endif

  bgp_prepare_daemon();
  skinny_bgp_daemon();

  return 0;
}
