/* Copyright 1999-2003 by Marco d'Itri <md@linux.it>.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

/* System library */
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <unistd.h>
#include "config.h"
#include <string.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <errno.h>
#include <signal.h>
#ifdef HAVE_GETOPT_LONG
#include <getopt.h>
#endif
#ifdef HAVE_REGEXEC
#include <regex.h>
#endif
#ifdef HAVE_LIBIDN
#include <idna.h>
#endif

/* Application-specific */
#include "data.h"
#include "whois.h"

/* Global variables */
int sockfd, verb = 0;

#ifdef ALWAYS_HIDE_DISCL
int hide_discl = HIDE_UNSTARTED;
#else
int hide_discl = HIDE_DISABLED;
#endif

char *client_tag = (char *)IDSTRING;

#ifdef HAVE_GETOPT_LONG
static struct option longopts[] = {
    {"help",	no_argument,		NULL, 0  },
    {"version",	no_argument,		NULL, 1  },
    {"verbose",	no_argument,		NULL, 2  },
    {"server",	required_argument,	NULL, 'h'},
    {"host",	required_argument,	NULL, 'h'},
    {"port",	required_argument,	NULL, 'p'},
    {NULL,	0,			NULL, 0  }
};
#else
extern char *optarg;
extern int optind;
#endif

int main(int argc, char *argv[])
{
    int ch, nopar = 0;
    const char *server = NULL, *port = NULL;
    char *p, *qstring, fstring[64] = "\0";

#ifdef ENABLE_NLS
    setlocale(LC_ALL, "");
    bindtextdomain(NLS_CAT_NAME, LOCALEDIR);
    textdomain(NLS_CAT_NAME);
#endif

    while ((ch = GETOPT_LONGISH(argc, argv, "acdFg:h:Hi:KlLmMp:q:rRs:St:T:v:V:x",
				longopts, 0)) > 0) {
	/* RIPE flags */
	if (strchr(ripeflags, ch)) {
	    for (p = fstring; *p; p++);
	    sprintf(p--, "-%c ", ch);
	    continue;
	}
	if (strchr(ripeflagsp, ch)) {
	    for (p = fstring; *p; p++);
	    snprintf(p--, sizeof(fstring), "-%c %s ", ch, optarg);
	    if (ch == 't' || ch == 'v' || ch == 'q')
		nopar = 1;
	    continue;
	}
	/* program flags */
	switch (ch) {
	case 'h':
	    server = strdup(optarg);
	    break;
	case 'V':
	    client_tag = optarg;
	case 'H':
	    hide_discl = HIDE_UNSTARTED;	/* enable disclaimers hiding */
	    break;
	case 'p':
	    port = strdup(optarg);
	    break;
	case 2:
	    verb = 1;
	    break;
	case 1:
#ifdef VERSION
	    fprintf(stderr, _("Version %s.\n\nReport bugs to %s.\n"),
		    VERSION, "<md+whois@linux.it>");
#else
	    fprintf(stderr, "%s %s\n", inetutils_package, inetutils_version);
#endif
	    exit(0);
	default:
	    usage();
	}
    }
    argc -= optind;
    argv += optind;

    if (argc == 0 && !nopar)	/* there is no parameter */
	usage();

    /* On some systems realloc only works on non-NULL buffers */
    qstring = malloc(64);
    *qstring = '\0';

    /* parse other parameters, if any */
    if (!nopar) {
	int qslen = 0;

	while (1) {
	    qslen += strlen(*argv) + 1 + 1;
	    qstring = realloc(qstring, qslen);
	    strcat(qstring, *argv++);
	    if (argc == 1)
		break;
	    strcat(qstring, " ");
	    argc--;
	}
    }

    signal(SIGTERM, sighandler);
    signal(SIGINT, sighandler);
    signal(SIGALRM, alarm_handler);

    if (getenv("WHOIS_HIDE"))
	hide_discl = HIDE_UNSTARTED;

    /* -v or -t has been used */
    if (!server && !*qstring)
	server = strdup("whois.ripe.net");

#ifdef CONFIG_FILE
    if (!server) {
	server = match_config_file(qstring);
	if (verb && server)
	    printf(_("Using server %s.\n"), server);
    }
#endif

    if (!server) {
	char *tmp;

	tmp = normalize_domain(qstring);
	free(qstring);
	qstring = tmp;
	server = whichwhois(qstring);

    }

    handle_query(server, port, qstring, fstring);

    exit(0);
}

/* server may be a server name from the command line, a server name got
 * from whichwhois or an encoded command/message from whichwhois.
 * server and port are allocated with malloc.
 */
const char *handle_query(const char *hserver, const char *hport,
	const char *qstring, const char *fstring)
{
    const char *server, *port = NULL;
    char *p;

    if (hport) {
	server = strdup(hserver);
	port = strdup(hport);
    } else
	split_server_port(hserver, &server, &port);

    switch (server[0]) {
	case 0:
	    if (!(server = getenv("WHOIS_SERVER")))
		server = DEFAULTSERVER;
	    break;
	case 1:
	    puts(_("This TLD has no whois server, but you can access the "
			"whois database at"));
	    puts(server + 1);
	    return NULL;
	case 2:
	    puts(server + 1);
	    return NULL;
	case 3:
	    puts(_("This TLD has no whois server."));
	    return NULL;
	case 5:
	    puts(_("No whois server is known for this kind of object."));
	    return NULL;
	case 6:
	    puts(_("Unknown AS number or IP network. Please upgrade this program."));
	    return NULL;
	case 4:
	    if (verb)
		puts(_("Connecting to whois.crsnic.net."));
	    sockfd = openconn("whois.crsnic.net", NULL);
	    server = query_crsnic(sockfd, qstring);
	    break;
	case 7:
	    if (verb)
		puts(_("Connecting to whois.publicinterestregistry.net."));
	    sockfd = openconn("whois.publicinterestregistry.net", NULL);
	    server = query_pir(sockfd, qstring);
	    break;
	case 9:
	    if (verb)
		puts(_("Connecting to whois.nic.cc."));
	    sockfd = openconn("whois.nic.cc", NULL);
	    server = query_crsnic(sockfd, qstring);
	    break;
	case 0x0A:
	    p = convert_6to4(qstring);
	    printf(_("\nQuerying for the IPv4 endpoint %s of a 6to4 IPv6 address.\n\n"), p);
	    server = whichwhois(p);
	    qstring = p;			/* XXX leak */
	    break;
	default:
	    break;
    }

    if (!server)
	return NULL;

    p = queryformat(server, fstring, qstring);
    if (verb) {
	printf(_("Using server %s.\n"), server);
	printf(_("Query string: \"%s\"\n\n"), p);
    }

    sockfd = openconn(server, port);

    strcat(p, "\r\n");
    server = do_query(sockfd, p);

    /* recursion is fun */
    if (server) {
	printf(_("\n\nFound a referral to %s.\n\n"), server);
	handle_query(server, NULL, qstring, fstring);
    }

    return NULL;
}

#ifdef CONFIG_FILE
const char *match_config_file(const char *s)
{
    FILE *fp;
    char buf[512];
    static const char delim[] = " \t";

    if ((fp = fopen(CONFIG_FILE, "r")) == NULL) {
	if (errno != ENOENT)
	    err_sys("Cannot open " CONFIG_FILE);
	return NULL;
    }

    while (fgets(buf, sizeof(buf), fp) != NULL) {
	char *p;
	const char *pattern, *server;
#ifdef HAVE_REGEXEC
	int i;
	regex_t re;
#endif

	for (p = buf; *p; p++)
	    if (*p == '\n')
		*p = '\0';

	p = buf;
	while (*p == ' ' || *p == '\t')	/* eat leading blanks */
	    p++;
	if (!*p)
	    continue;		/* skip empty lines */
	if (*p == '#')
	    continue;		/* skip comments */

	pattern = strtok(p, delim);
	server = strtok(NULL, delim);
	if (!pattern || !server)
	    err_quit(_("Cannot parse this line: %s"), p);
	p = strtok(NULL, delim);
	if (p)
	    err_quit(_("Cannot parse this line: %s"), p);

#ifdef HAVE_REGEXEC
	i = regcomp(&re, pattern, REG_EXTENDED|REG_ICASE|REG_NOSUB);
	if (i != 0) {
	    char m[1024];
	    regerror(i, &re, m, sizeof(m));
	    err_quit("Invalid regular expression '%s': %s", pattern, m);
	}

	i = regexec(&re, s, 0, NULL, 0);
	if (i == 0) {
	    regfree(&re);
	    return strdup(server);
	}
	if (i != REG_NOMATCH) {
	    char m[1024];
	    regerror(i, &re, m, sizeof(m));
	    err_quit("regexec: %s",  m);
	}
	regfree(&re);
#else
	if (domcmp(s, pattern))
	    return strdup(server);
#endif
    }
    return NULL;
}
#endif

/* Parses an user-supplied string and tries to guess the right whois server.
 * Returns a statically allocated buffer.
 */
const char *whichwhois(const char *s)
{
    unsigned long ip;
    unsigned int i;

    /* IPv6 address */
    if (strchr(s, ':')) {
	if (strncmp(s, "2001:",  5) == 0) {
	    unsigned long v6net = strtol(s + 5, NULL, 16);
	    v6net = v6net & 0xfe00;	/* we care about the first 7 bits */
	    for (i = 0; ip6_assign[i].serv; i++)
		if (v6net == ip6_assign[i].net)
		    return ip6_assign[i].serv;
	    return "\x06";			/* unknown allocation */
	} else if (strncmp(s, "2002:", 5) == 0) {
	    return "\x0A";
	} else if (strncasecmp(s, "3ffe:", 5) == 0)
	    return "whois.6bone.net";
	/* RPSL hierarchical object like AS8627:fltr-TRANSIT-OUT */
	else if (strncasecmp(s, "as", 2) == 0 && isasciidigit(s[2]))
	    return whereas(atoi(s + 2));
	else
	    return "\x05";
    }

    /* email address */
    if (strchr(s, '@'))
	return "\x05";

    /* no dot and no hyphen means it's a NSI NIC handle or ASN (?) */
    if (!strpbrk(s, ".-")) {
	const char *p;

	for (p = s; *p; p++);			/* go to the end of s */
	if (strncasecmp(s, "as", 2) == 0 &&	/* it's an AS */
		(isasciidigit(s[2]) || s[2] == ' '))
	    return whereas(atoi(s + 2));
	else if (strncasecmp(p - 2, "jp", 2) == 0) /* JP NIC handle */
	    return "whois.nic.ad.jp";
	if (*s == '!')	/* NSI NIC handle */
	    return "whois.networksolutions.com";
	else
	    return "\x05";	/* probably a unknown kind of nic handle */
    }

    /* smells like an IP? */
    if ((ip = myinet_aton(s))) {
	for (i = 0; ip_assign[i].serv; i++)
	    if ((ip & ip_assign[i].mask) == ip_assign[i].net)
		return ip_assign[i].serv;
	return "\x05";			/* not in the unicast IPv4 space */
    }

    /* check the TLDs list */
    for (i = 0; tld_serv[i]; i += 2)
	if (domcmp(s, tld_serv[i]))
	    return tld_serv[i + 1];

    /* no dot but hyphen */
    if (!strchr(s, '.')) {
	/* search for strings at the start of the word */
	for (i = 0; nic_handles[i]; i += 2)
	    if (strncasecmp(s, nic_handles[i], strlen(nic_handles[i])) == 0)
		return nic_handles[i + 1];
	/* it's probably a network name */
	return "";
    }

    /* has dot and maybe a hyphen and it's not in tld_serv[], WTF is it? */
    /* either a TLD or a NIC handle we don't know about yet */
    return "\x05";
}

const char *whereas(const unsigned short asn)
{
    int i;

    for (i = 0; as_assign[i].serv; i++)
	if (asn >= as_assign[i].first && asn <= as_assign[i].last)
	    return as_assign[i].serv;
    return "\x06";
}

char *queryformat(const char *server, const char *flags, const char *query)
{
    char *buf;
    int i, isripe = 0;

    /* 64 bytes reserved for server-specific flags added later */
    buf = malloc(strlen(flags) + strlen(query) + strlen(client_tag) + 64);
    *buf = '\0';
    for (i = 0; ripe_servers[i]; i++)
	if (strcmp(server, ripe_servers[i]) == 0) {
	    strcat(buf, "-V ");
	    strcat(buf, client_tag);
	    strcat(buf, " ");
	    isripe = 1;
	    break;
	}
    if (!isripe)
	for (i = 0; ripe_servers_old[i]; i++)
	    if (strcmp(server, ripe_servers_old[i]) == 0) {
		strcat(buf, "-V");
		strcat(buf, client_tag);
		strcat(buf, " ");
		isripe = 1;
		break;
	    }
    if (*flags) {
	if (!isripe && strcmp(server, "whois.corenic.net") != 0)
	    puts(_("Warning: RIPE flags used with a traditional server."));
	strcat(buf, flags);
    }

    /* why, oh why DENIC had to make whois "user friendly"?
     * I hope that adding -T dn,ace will not break some queries.
     */
    if (isripe && strcmp(server, "whois.denic.de") == 0 && domcmp(query, ".de")
	    && !strchr(query, ' '))
	sprintf(buf, "-T dn,ace -C US-ASCII %s", query);
    else if (!isripe && (strcmp(server, "whois.nic.mil") == 0 ||
	    strcmp(server, "whois.nic.ad.jp") == 0) &&
	    strncasecmp(query, "AS", 2) == 0 && isasciidigit(query[2]))
	/* FIXME: /e is not applied to .JP ASN */
	sprintf(buf, "AS %s", query + 2);	/* fix query for DDN */
    else if (!isripe && strcmp(server, "whois.nic.ad.jp") == 0) {
	char *lang = getenv("LANG");	/* not a perfect check, but... */
	if (!lang || (strncmp(lang, "ja", 2) != 0))
	    sprintf(buf, "%s/e", query);	/* ask for english text */
	else
	    strcat(buf, query);
    } else
	strcat(buf, query);
    return buf;
}

/* the first parameter contains the state of this simple state machine:
 * HIDE_DISABLED: hidden text finished
 * HIDE_UNSTARTED: hidden text not seen yet
 * >= 0: currently hiding message hide_strings[*hiding]
 */
int hide_line(int *hiding, const char *const line)
{
    int i;

    if (*hiding == HIDE_DISABLED) {
	return 0;
    } else if (*hiding == HIDE_UNSTARTED) {	/* looking for smtng to hide */
	for (i = 0; hide_strings[i] != NULL; i += 2) {
	    if (strncmp(line, hide_strings[i], strlen(hide_strings[i])) == 0) {
		*hiding = i;			/* start hiding */
		return 1;			/* and hide this line */
	    }
	}
	return 0;				/* don't hide this line */
    } else if (*hiding > HIDE_UNSTARTED) {	/* hiding something */
	if (strncmp(line, hide_strings[*hiding + 1],
		    strlen(hide_strings[*hiding + 1])) == 0) {
	    *hiding = HIDE_DISABLED;		/* stop hiding */
	    return 1;				/* but hide the last line */
	}
	return 1;				/* we are hiding, so do it */
    }
#if 0
    /* XXX */
    /* no match, the action depends on the state stored in *hiding */
    if (*hiding > HIDE_UNSTARTED)
	return 1;
    else
#endif
	return 0;
}

/* returns a string which should be freed by the caller, or NULL */
const char *do_query(const int sock, const char *query)
{
    char buf[2000], *p;
    FILE *fi;
    int hide = hide_discl;
    char *referral_server = NULL;

    fi = fdopen(sock, "r");
    if (write(sock, query, strlen(query)) < 0)
	err_sys("write");
/* Using shutdown breaks the buggy RIPE server.
    if (shutdown(sock, 1) < 0)
	err_sys("shutdown");
*/

    while (fgets(buf, sizeof(buf), fi)) {
	/* 6bone-style referral:
	 * % referto: whois -h whois.arin.net -p 43 as 1
	 */
	if (!referral_server && strncmp(buf, "% referto:", 10) == 0) {
	    char nh[256], np[16], nq[1024];

	    if (sscanf(buf, REFERTO_FORMAT, nh, np, nq) == 3) {
		/* XXX we are ignoring the new query string */
		referral_server = malloc(300);
		sprintf(referral_server, "%s:%s", nh, np);
	    }
	}

	/* ARIN referrals:
	 * ReferralServer: rwhois://rwhois.fuse.net:4321/
	 * ReferralServer: whois://whois.ripe.net
	 */
	if (!referral_server && strncmp(buf, "ReferralServer:", 15) == 0) {
	    char *q;

	    q = strstr(buf, "rwhois://");
	    if ((q = strstr(buf, "rwhois://")))
		referral_server = strdup(q + 9);
	    else if ((q = strstr(buf, "whois://")))
		referral_server = strdup(q + 8);
	    if (referral_server) {
		if ((q = strchr(referral_server, '/'))
			|| (q = strchr(referral_server, '\n')))
		    *q = '\0';
	    }
	}

	if (hide_line(&hide, buf))
	    continue;

	for (p = buf; *p && *p != '\r' && *p != '\n'; p++);
	*p = '\0';
	fprintf(stdout, "%s\n", buf);
    }
    if (ferror(fi))
	err_sys("fgets");
    fclose(fi);

    if (hide > HIDE_UNSTARTED)
	err_quit(_("Catastrophic error: disclaimer text has been changed.\n"
		   "Please upgrade this program.\n"));

    return referral_server;
}

const char *query_crsnic(const int sock, const char *query)
{
    char *temp, buf[2000], *ret = NULL;
    FILE *fi;
    int hide = hide_discl;
    int state = 0;

    temp = malloc(strlen(query) + 1 + 2 + 1);
    *temp = '=';
    strcpy(temp + 1, query);
    strcat(temp, "\r\n");

    fi = fdopen(sock, "r");
    if (write(sock, temp, strlen(temp)) < 0)
	err_sys("write");
    while (fgets(buf, sizeof(buf), fi)) {
	/* If there are multiple matches only the server of the first record
	   is queried */
	if (state == 0 && strncmp(buf, "   Domain Name:", 15) == 0)
	    state = 1;
	if (state == 1 && strncmp(buf, "   Whois Server:", 16) == 0) {
	    char *p, *q;

	    for (p = buf; *p != ':'; p++);	/* skip until colon */
	    for (p++; *p == ' '; p++);		/* skip colon and spaces */
	    ret = malloc(strlen(p) + 1);
	    for (q = ret; *p != '\n' && *p != '\r' && *p != ' '; *q++ = *p++)
		; /*copy data*/
	    *q = '\0';
	    state = 2;
	}
	/* the output must not be hidden or no data will be shown for
	   host records and not-existing domains */
	if (!hide_line(&hide, buf))
	    fputs(buf, stdout);
    }
    if (ferror(fi))
	err_sys("fgets");

    free(temp);
    return ret;
}

const char *query_pir(const int sock, const char *query)
{
    char *temp, buf[2000], *ret = NULL;
    FILE *fi;
    int hide = hide_discl;
    int state = 0;

    temp = malloc(strlen(query) + 5 + 2 + 1);
    strcpy(temp, "FULL ");
    strcat(temp, query);
    strcat(temp, "\r\n");

    fi = fdopen(sock, "r");
    if (write(sock, temp, strlen(temp)) < 0)
	err_sys("write");
    while (fgets(buf, sizeof(buf), fi)) {
	/* If there are multiple matches only the server of the first record
	   is queried */
	if (state == 0 &&
    strncmp(buf, "Registrant Name:SEE SPONSORING REGISTRAR", 40) == 0)
	    state = 1;
	if (state == 1 &&
		strncmp(buf, "Registrant Street1:Whois Server:", 32) == 0) {
	    char *p, *q;

	    for (p = buf; *p != ':'; p++);	/* skip until colon */
	    for (p++; *p != ':'; p++);		/* skip until 2nd colon */
	    for (p++; *p == ' '; p++);		/* skip colon and spaces */
	    ret = malloc(strlen(p) + 1);
	    for (q = ret; *p != '\n' && *p != '\r'; *q++ = *p++); /*copy data*/
	    *q = '\0';
	    state = 2;
	}
	if (!hide_line(&hide, buf))
	    fputs(buf, stdout);
    }
    if (ferror(fi))
	err_sys("fgets");

    free(temp);
    return ret;
}

int openconn(const char *server, const char *port)
{
    int fd = -1;
#ifdef HAVE_GETADDRINFO
    int err;
    struct addrinfo hints, *res, *ai;
#else
    struct hostent *hostinfo;
    struct servent *servinfo;
    struct sockaddr_in saddr;
#endif

    alarm(60);

#ifdef HAVE_GETADDRINFO
    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    if ((err = getaddrinfo(server, port ? port : "whois", &hints, &res)) != 0)
	err_quit("getaddrinfo: %s", gai_strerror(err));
    for (ai = res; ai; ai = ai->ai_next) {
	if ((fd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol)) < 0)
	    continue;		/* ignore */
	if (connect(fd, (struct sockaddr *)ai->ai_addr, ai->ai_addrlen) == 0)
	    break;		/* success */
	close(fd);
    }
    freeaddrinfo(res);

    if (!ai)
	err_sys("connect");
#else
    if ((hostinfo = gethostbyname(server)) == NULL)
	err_quit(_("Host %s not found."), server);
    if ((fd = socket(PF_INET, SOCK_STREAM, IPPROTO_IP)) < 0)
	err_sys("socket");
    memset(&saddr, 0, sizeof(saddr));
    saddr.sin_addr = *(struct in_addr *) hostinfo->h_addr;
    saddr.sin_family = AF_INET;
    if (!port) {
	saddr.sin_port = htons(43);
    } else if ((saddr.sin_port = htons(atoi(port))) == 0) {
	if ((servinfo = getservbyname(port, "tcp")) == NULL)
	    err_quit(_("%s/tcp: unknown service"), port);
	saddr.sin_port = servinfo->s_port;
    }
    if (connect(fd, (struct sockaddr *)&saddr, sizeof(saddr)) < 0)
	err_sys("connect");
#endif

    /*
     * Now we are connected and the query is supposed to complete quickly.
     * This will help people who run whois ... | less
     */
    alarm(0);

    return fd;
}

void alarm_handler(int signum)
{
    close(sockfd);
    err_quit(_("Timeout."));
}

void sighandler(int signum)
{
    close(sockfd);
    err_quit(_("Interrupted by signal %d..."), signum);
}

/* check if dom ends with tld */
int domcmp(const char *dom, const char *tld)
{
    const char *p, *q;

    for (p = dom; *p; p++); p--;	/* move to the last char */
    for (q = tld; *q; q++); q--;
    while (p >= dom && q >= tld && tolower(*p) == *q) {	/* compare backwards */
	if (q == tld)			/* start of the second word? */
	    return 1;
	p--; q--;
    }
    return 0;
}

char *normalize_domain(const char *dom)
{
    char *p, *ret;

    ret = strdup(dom);
    for (p = ret; *p; p++); p--;	/* move to the last char */
    for (; *p == '.' || p == ret; p--)	/* eat trailing dots */
	*p = '\0';

#ifdef HAVE_LIBIDN
    if (idna_to_ascii_lz(ret, &p, 0) != IDNA_SUCCESS) {
	free(ret);
	return ret;
    }

    free(ret);
    ret = p;
#endif

    return ret;
}

/* server and port have to be freed by the caller */
void split_server_port(const char *const input,
	const char **server, const char **port) {
    char *q, *p;

    *server = q = strdup(input);

    for (p = q; *p && *p != ':'; *q++ = tolower(*p++));
    if (*p == ':')
	*port = strdup(p + 1);
    *p = '\0';
}

char *convert_6to4(const char *s)
{
    char *new = malloc(sizeof("255.255.255.255"));
    unsigned int a, b;

    if (sscanf(s, "2002:%x:%x:", &a, &b) != 2)
	return (char *) "0.0.0.0";

    sprintf(new, "%d.%d.%d.%d", a >> 8, a & 0xff, b >> 8, b & 0xff);
    return new;
}

unsigned long myinet_aton(const char *s)
{
    unsigned long a, b, c, d;

    if (!s)
	return 0;
    if (sscanf(s, "%lu.%lu.%lu.%lu", &a, &b, &c, &d) != 4)
	return 0;
    if (a > 255 || b > 255 || c > 255 || d > 255)
	return 0;
    return (a << 24) + (b << 16) + (c << 8) + d;
}

int isasciidigit(const char c) {
    return (c >= '0' && c <= '9') ? 1 : 0;
}

/* http://www.ripe.net/ripe/docs/databaseref-manual.html */

void usage(void)
{
    fprintf(stderr, _(
"Usage: whois [OPTION]... OBJECT...\n\n"
"-l                     one level less specific lookup [RPSL only]\n"
"-L                     find all Less specific matches\n"
"-m                     find first level more specific matches\n"
"-M                     find all More specific matches\n"
"-c                     find the smallest match containing a mnt-irt attribute\n"
"-x                     exact match [RPSL only]\n"
"-d                     return DNS reverse delegation objects too [RPSL only]\n"
"-i ATTR[,ATTR]...      do an inverse lookup for specified ATTRibutes\n"
"-T TYPE[,TYPE]...      only look for objects of TYPE\n"
"-K                     only primary keys are returned [RPSL only]\n"
"-r                     turn off recursive lookups for contact information\n"
"-R                     force to show local copy of the domain object even\n"
"                       if it contains referral\n"
"-a                     search all databases\n"
"-s SOURCE[,SOURCE]...  search the database from SOURCE\n"
"-g SOURCE:FIRST-LAST   find updates from SOURCE from serial FIRST to LAST\n"
"-t TYPE                request template for object of TYPE ('all' for a list)\n"
"-v TYPE                request verbose template for object of TYPE\n"
"-q [version|sources|types]  query specified server info [RPSL only]\n"
"-F                     fast raw output (implies -r)\n"
"-h HOST                connect to server HOST\n"
"-p PORT                connect to PORT\n"
"-H                     hide legal disclaimers\n"
"      --verbose        explain what is being done\n"
"      --help           display this help and exit\n"
"      --version        output version information and exit\n"
));
    exit(0);
}


/* Error routines */
void err_sys(const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, ": %s\n", strerror(errno));
    va_end(ap);
    exit(2);
}

void err_quit(const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    fputs("\n", stderr);
    va_end(ap);
    exit(2);
}

