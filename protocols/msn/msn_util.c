  /********************************************************************\
  * BitlBee -- An IRC to other IM-networks gateway                     *
  *                                                                    *
  * Copyright 2002-2010 Wilmer van der Gaast and others                *
  \********************************************************************/

/* MSN module - Miscellaneous utilities                                 */

/*
  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License with
  the Debian GNU/Linux distribution in /usr/share/common-licenses/GPL;
  if not, write to the Free Software Foundation, Inc., 59 Temple Place,
  Suite 330, Boston, MA  02111-1307  USA
*/

#include "nogaim.h"
#include "msn.h"
#include "md5.h"
#include "soap.h"
#include <ctype.h>

int msn_write( struct im_connection *ic, char *s, int len )
{
	struct msn_data *md = ic->proto_data;
	int st;
	
	if( getenv( "BITLBEE_DEBUG" ) )
	{
		write( 2, "->NS:", 5 );
		write( 2, s, len );
	}
	
	st = write( md->fd, s, len );
	if( st != len )
	{
		imcb_error( ic, "Short write() to main server" );
		imc_logout( ic, TRUE );
		return 0;
	}
	
	return 1;
}

int msn_logged_in( struct im_connection *ic )
{
	imcb_connected( ic );
	
	return( 0 );
}

static char *adlrml_entry( const char *handle_, msn_buddy_flags_t list )
{
	char *domain, handle[strlen(handle_)+1];
	
	strcpy( handle, handle_ );
	if( ( domain = strchr( handle, '@' ) ) )
		*(domain++) = '\0';
	else
		return NULL;
	
	return g_markup_printf_escaped( "<ml><d n=\"%s\"><c n=\"%s\" l=\"%d\" t=\"1\"/></d></ml>",
		domain, handle, list );
}

int msn_buddy_list_add( struct im_connection *ic, msn_buddy_flags_t list, const char *who, const char *realname, const char *group )
{
	struct msn_data *md = ic->proto_data;
	char buf[1024], groupid[8];
	bee_user_t *bu;
	struct msn_buddy_data *bd;
	char *adl;
	
	*groupid = '\0';
#if 0
	if( group )
	{
		int i;
		for( i = 0; i < md->groupcount; i ++ )
			if( g_strcasecmp( md->grouplist[i], group ) == 0 )
			{
				g_snprintf( groupid, sizeof( groupid ), " %d", i );
				break;
			}
		
		if( *groupid == '\0' )
		{
			/* Have to create this group, it doesn't exist yet. */
			struct msn_groupadd *ga;
			GSList *l;
			
			for( l = md->grpq; l; l = l->next )
			{
				ga = l->data;
				if( g_strcasecmp( ga->group, group ) == 0 )
					break;
			}
			
			ga = g_new0( struct msn_groupadd, 1 );
			ga->who = g_strdup( who );
			ga->group = g_strdup( group );
			md->grpq = g_slist_prepend( md->grpq, ga );
			
			if( l == NULL )
			{
				char groupname[strlen(group)+1];
				strcpy( groupname, group );
				http_encode( groupname );
				g_snprintf( buf, sizeof( buf ), "ADG %d %s %d\r\n", ++md->trId, groupname, 0 );
				return msn_write( ic, buf, strlen( buf ) );
			}
			else
			{
				/* This can happen if the user's doing lots of adds to a
				   new group at once; we're still waiting for the server
				   to confirm group creation. */
				return 1;
			}
		}
	}
#endif
	
	if( !( ( bu = bee_user_by_handle( ic->bee, ic, who ) ) ||
	       ( bu = bee_user_new( ic->bee, ic, who, 0 ) ) ) ||
	    !( bd = bu->data ) || bd->flags & list )
		return 1;
	
	bd->flags |= list;
	
	if( list == MSN_BUDDY_FL )
		msn_soap_ab_contact_add( ic, bu );
	else
		msn_soap_memlist_edit( ic, who, TRUE, list );
	
	if( ( adl = adlrml_entry( who, list ) ) )
	{
		g_snprintf( buf, sizeof( buf ), "ADL %d %zd\r\n%s",
		            ++md->trId, strlen( adl ), adl );
		g_free( adl );
		
		return msn_write( ic, buf, strlen( buf ) );
	}
	
	return 1;
}

int msn_buddy_list_remove( struct im_connection *ic, msn_buddy_flags_t list, const char *who, const char *group )
{
	struct msn_data *md = ic->proto_data;
	char buf[1024], groupid[8];
	bee_user_t *bu;
	struct msn_buddy_data *bd;
	char *adl;
	
	*groupid = '\0';
#if 0
	if( group )
	{
		int i;
		for( i = 0; i < md->groupcount; i ++ )
			if( g_strcasecmp( md->grouplist[i], group ) == 0 )
			{
				g_snprintf( groupid, sizeof( groupid ), " %d", i );
				break;
			}
	}
#endif
	
	if( !( bu = bee_user_by_handle( ic->bee, ic, who ) ) ||
	    !( bd = bu->data ) || !( bd->flags & list ) )
		return 1;
	
	bd->flags &= ~list;
	
	if( list == MSN_BUDDY_FL )
		msn_soap_ab_contact_del( ic, bu );
	else
		msn_soap_memlist_edit( ic, who, FALSE, list );
	
	if( ( adl = adlrml_entry( who, list ) ) )
	{
		g_snprintf( buf, sizeof( buf ), "RML %d %zd\r\n%s",
		            ++md->trId, strlen( adl ), adl );
		g_free( adl );
		
		return msn_write( ic, buf, strlen( buf ) );
	}
	
	return 1;
}

struct msn_buddy_ask_data
{
	struct im_connection *ic;
	char *handle;
	char *realname;
};

static void msn_buddy_ask_yes( void *data )
{
	struct msn_buddy_ask_data *bla = data;
	
	msn_buddy_list_add( bla->ic, MSN_BUDDY_AL, bla->handle, bla->realname, NULL );
	
	imcb_ask_add( bla->ic, bla->handle, NULL );
	
	g_free( bla->handle );
	g_free( bla->realname );
	g_free( bla );
}

static void msn_buddy_ask_no( void *data )
{
	struct msn_buddy_ask_data *bla = data;
	
	msn_buddy_list_add( bla->ic, MSN_BUDDY_BL, bla->handle, bla->realname, NULL );
	
	g_free( bla->handle );
	g_free( bla->realname );
	g_free( bla );
}

void msn_buddy_ask( bee_user_t *bu )
{
	struct msn_buddy_ask_data *bla;
	struct msn_buddy_data *bd = bu->data;
	char buf[1024];
	
	if( ( bd->flags & 30 ) != 8 && ( bd->flags & 30 ) != 16 )
		return;
	
	bla = g_new0( struct msn_buddy_ask_data, 1 );
	bla->ic = bu->ic;
	bla->handle = g_strdup( bu->handle );
	bla->realname = g_strdup( bu->fullname );
	
	g_snprintf( buf, sizeof( buf ),
	            "The user %s (%s) wants to add you to his/her buddy list.",
	            bu->handle, bu->fullname );
	imcb_ask( bu->ic, buf, bla, msn_buddy_ask_yes, msn_buddy_ask_no );
}

char *msn_findheader( char *text, char *header, int len )
{
	int hlen = strlen( header ), i;
	char *ret;
	
	if( len == 0 )
		len = strlen( text );
	
	i = 0;
	while( ( i + hlen ) < len )
	{
		/* Maybe this is a bit over-commented, but I just hate this part... */
		if( g_strncasecmp( text + i, header, hlen ) == 0 )
		{
			/* Skip to the (probable) end of the header */
			i += hlen;
			
			/* Find the first non-[: \t] character */
			while( i < len && ( text[i] == ':' || text[i] == ' ' || text[i] == '\t' ) ) i ++;
			
			/* Make sure we're still inside the string */
			if( i >= len ) return( NULL );
			
			/* Save the position */
			ret = text + i;
			
			/* Search for the end of this line */
			while( i < len && text[i] != '\r' && text[i] != '\n' ) i ++;
			
			/* Make sure we're still inside the string */
			if( i >= len ) return( NULL );
			
			/* Copy the found data */
			return( g_strndup( ret, text + i - ret ) );
		}
		
		/* This wasn't the header we were looking for, skip to the next line. */
		while( i < len && ( text[i] != '\r' && text[i] != '\n' ) ) i ++;
		while( i < len && ( text[i] == '\r' || text[i] == '\n' ) ) i ++;
		
		/* End of headers? */
		if( ( i >= 4 && strncmp( text + i - 4, "\r\n\r\n", 4 ) == 0 ) ||
		    ( i >= 2 && ( strncmp( text + i - 2, "\n\n", 2 ) == 0 ||   
		                  strncmp( text + i - 2, "\r\r", 2 ) == 0 ) ) )
		{
			break;
		}
	}
	
	return( NULL );
}

/* *NOT* thread-safe, but that's not a problem for now... */
char **msn_linesplit( char *line )
{
	static char **ret = NULL;
	static int size = 3;
	int i, n = 0;
	
	if( ret == NULL )
		ret = g_new0( char*, size );
	
	for( i = 0; line[i] && line[i] == ' '; i ++ );
	if( line[i] )
	{
		ret[n++] = line + i;
		for( i ++; line[i]; i ++ )
		{
			if( line[i] == ' ' )
				line[i] = 0;
			else if( line[i] != ' ' && !line[i-1] )
				ret[n++] = line + i;
			
			if( n >= size )
				ret = g_renew( char*, ret, size += 2 );
		}
	}
	ret[n] = NULL;
	
	return( ret );
}

/* This one handles input from a MSN Messenger server. Both the NS and SB servers usually give
   commands, but sometimes they give additional data (payload). This function tries to handle
   this all in a nice way and send all data to the right places. */

/* Return values: -1: Read error, abort connection.
                   0: Command reported error; Abort *immediately*. (The connection does not exist anymore)
                   1: OK */

int msn_handler( struct msn_handler_data *h )
{
	int st;
	
	h->rxq = g_renew( char, h->rxq, h->rxlen + 1024 );
	st = read( h->fd, h->rxq + h->rxlen, 1024 );
	h->rxlen += st;
	
	if( st <= 0 )
		return( -1 );
	
	if( getenv( "BITLBEE_DEBUG" ) )
	{
		write( 2, "->C:", 4 );
		write( 2, h->rxq + h->rxlen - st, st );
	}
	
	while( st )
	{
		int i;
		
		if( h->msglen == 0 )
		{
			for( i = 0; i < h->rxlen; i ++ )
			{
				if( h->rxq[i] == '\r' || h->rxq[i] == '\n' )
				{
					char *cmd_text, **cmd;
					int count;
					
					cmd_text = g_strndup( h->rxq, i );
					cmd = msn_linesplit( cmd_text );
					for( count = 0; cmd[count]; count ++ );
					st = h->exec_command( h->data, cmd, count );
					g_free( cmd_text );
					
					/* If the connection broke, don't continue. We don't even exist anymore. */
					if( !st )
						return( 0 );
					
					if( h->msglen )
						h->cmd_text = g_strndup( h->rxq, i );
					
					/* Skip to the next non-emptyline */
					while( i < h->rxlen && ( h->rxq[i] == '\r' || h->rxq[i] == '\n' ) ) i ++;
					
					break;
				}
			}
			
			/* If we reached the end of the buffer, there's still an incomplete command there.
			   Return and wait for more data. */
			if( i == h->rxlen && h->rxq[i-1] != '\r' && h->rxq[i-1] != '\n' )
				break;
		}
		else
		{
			char *msg, **cmd;
			int count;
			
			/* Do we have the complete message already? */
			if( h->msglen > h->rxlen )
				break;
			
			msg = g_strndup( h->rxq, h->msglen );
			cmd = msn_linesplit( h->cmd_text );
			for( count = 0; cmd[count]; count ++ );
			
			st = h->exec_message( h->data, msg, h->msglen, cmd, count );
			g_free( msg );
			g_free( h->cmd_text );
			h->cmd_text = NULL;
			
			if( !st )
				return( 0 );
			
			i = h->msglen;
			h->msglen = 0;
		}
		
		/* More data after this block? */
		if( i < h->rxlen )
		{
			char *tmp;
			
			tmp = g_memdup( h->rxq + i, h->rxlen - i );
			g_free( h->rxq );
			h->rxq = tmp;
			h->rxlen -= i;
			i = 0;
		}
		else
		/* If not, reset the rx queue and get lost. */
		{
			g_free( h->rxq );
			h->rxq = g_new0( char, 1 );
			h->rxlen = 0;
			return( 1 );
		}
	}
	
	return( 1 );
}

void msn_msgq_purge( struct im_connection *ic, GSList **list )
{
	struct msn_message *m;
	GString *ret;
	GSList *l;
	int n = 0;
	
	l = *list;
	if( l == NULL )
		return;
	
	m = l->data;
	ret = g_string_sized_new( 1024 );
	g_string_printf( ret, "Warning: Cleaning up MSN (switchboard) connection with unsent "
	                      "messages to %s:", m->who ? m->who : "unknown recipient" );
	
	while( l )
	{
		m = l->data;
		
		if( strncmp( m->text, "\r\r\r", 3 ) != 0 )
		{
			g_string_append_printf( ret, "\n%s", m->text );
			n ++;
		}
		
		g_free( m->who );
		g_free( m->text );
		g_free( m );
		
		l = l->next;
	}
	g_slist_free( *list );
	*list = NULL;
	
	if( n > 0 )
		imcb_log( ic, "%s", ret->str );
	g_string_free( ret, TRUE );
}

/* Copied and heavily modified from http://tmsnc.sourceforge.net/chl.c */
char *msn_p11_challenge( char *challenge )
{
	char *output, buf[256];
	md5_state_t md5c;
	unsigned char md5Hash[16], *newHash;
	unsigned int *md5Parts, *chlStringParts, newHashParts[5];
	long long nHigh = 0, nLow = 0;
	int i, n;

	/* Create the MD5 hash */
	md5_init(&md5c);
	md5_append(&md5c, (unsigned char*) challenge, strlen(challenge));
	md5_append(&md5c, (unsigned char*) MSNP11_PROD_KEY, strlen(MSNP11_PROD_KEY));
	md5_finish(&md5c, md5Hash);

	/* Split it into four integers */
	md5Parts = (unsigned int *)md5Hash;
	for (i = 0; i < 4; i ++)
	{  
		md5Parts[i] = GUINT32_TO_LE(md5Parts[i]);
		
		/* & each integer with 0x7FFFFFFF */
		/* and save one unmodified array for later */
		newHashParts[i] = md5Parts[i];
		md5Parts[i] &= 0x7FFFFFFF;
	}
	
	/* make a new string and pad with '0' */
	n = g_snprintf(buf, sizeof(buf)-5, "%s%s00000000", challenge, MSNP11_PROD_ID);
	/* truncate at an 8-byte boundary */
	buf[n&=~7] = '\0';
	
	/* split into integers */
	chlStringParts = (unsigned int *)buf;
	
	/* this is magic */
	for (i = 0; i < (n / 4) - 1; i += 2)
	{
		long long temp;

		chlStringParts[i]   = GUINT32_TO_LE(chlStringParts[i]);
		chlStringParts[i+1] = GUINT32_TO_LE(chlStringParts[i+1]);

		temp  = (md5Parts[0] * (((0x0E79A9C1 * (long long)chlStringParts[i]) % 0x7FFFFFFF)+nHigh) + md5Parts[1])%0x7FFFFFFF;
		nHigh = (md5Parts[2] * (((long long)chlStringParts[i+1]+temp) % 0x7FFFFFFF) + md5Parts[3]) % 0x7FFFFFFF;
		nLow  = nLow + nHigh + temp;
	}
	nHigh = (nHigh+md5Parts[1]) % 0x7FFFFFFF;
	nLow = (nLow+md5Parts[3]) % 0x7FFFFFFF;
	
	newHashParts[0] ^= nHigh;
	newHashParts[1] ^= nLow;
	newHashParts[2] ^= nHigh;
	newHashParts[3] ^= nLow;
	
	/* swap more bytes if big endian */
	for (i = 0; i < 4; i ++)
		newHashParts[i] = GUINT32_TO_LE(newHashParts[i]); 
	
	/* make a string of the parts */
	newHash = (unsigned char *)newHashParts;
	
	/* convert to hexadecimal */
	output = g_new(char, 33);
	for (i = 0; i < 16; i ++)
		sprintf(output + i * 2, "%02x", newHash[i]);
	
	return output;
}

gint msn_domaintree_cmp( gconstpointer a_, gconstpointer b_ )
{
	const char *a = a_, *b = b_;
	gint ret;
	
	if( !( a = strchr( a, '@' ) ) || !( b = strchr( b, '@' ) ) ||
	    ( ret = strcmp( a, b ) ) == 0 )
		ret = strcmp( a_, b_ );
	
	return ret;
}
