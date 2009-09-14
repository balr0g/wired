/* $Id$ */

/*
 *  Copyright (c) 2009 Axel Andersson
 *  All rights reserved.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions
 *  are met:
 *  1. Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *  2. Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include "config.h"

#include <wired/wired.h>

#include <stdlib.h>
#include <string.h>

#include "natpmp/natpmp.h"
#include "miniupnpc/miniupnpc.h"
#include "miniupnpc/miniwget.h"
#include "miniupnpc/upnpcommands.h"

#include "portmap.h"
#include "server.h"

static struct UPNPUrls			wd_portmap_upnp_urls;
static struct IGDdatas			wd_portmap_upnp_data;


void wd_portmap_map_natpmp(void) {
	wi_address_t		*address = NULL;
	natpmp_t			natpmp;
	natpmpresp_t		response;
	fd_set				fds;
	struct timeval		tv;
	wi_uinteger_t		i;
	int					result;
	
	if(initnatpmp(&natpmp) == 0) {
		if(sendpublicaddressrequest(&natpmp)) {
			while(true) {
				FD_ZERO(&fds);
				FD_SET(natpmp.s, &fds);
				
				getnatpmprequesttimeout(&natpmp, &tv);
				
				select(natpmp.s, &fds, NULL, NULL, &tv);
				
				result = readnatpmpresponseorretry(&natpmp, &response);
				
				if(result != NATPMP_TRYAGAIN) {
					if(result == 0)
						address = wi_autorelease(wi_address_init_with_ipv4_address(wi_address_alloc(), response.pnu.publicaddress.addr.s_addr));
					
					break;
				}
			}
		}
		
		closenatpmp(&natpmp);
	}
	
	for(i = 0; i < 2; i++) {
		if(initnatpmp(&natpmp) == 0) {
			if(sendnewportmappingrequest(&natpmp,
										 (i == 0) ? NATPMP_PROTOCOL_TCP : NATPMP_PROTOCOL_UDP,
										 wd_port,
										 wd_port,
										 365 * 24 * 60 * 60)) {
				while(true) {
					FD_ZERO(&fds);
					FD_SET(natpmp.s, &fds);
					
					getnatpmprequesttimeout(&natpmp, &tv);
					
					select(natpmp.s, &fds, NULL, NULL, &tv);
					
					result = readnatpmpresponseorretry(&natpmp, &response);
					
					if(result != NATPMP_TRYAGAIN) {
						if(result == 0) {
							wi_log_info(WI_STR("Mapped internal %@ port %u to external port %u on %@ using NAT-PMP"),
										(response.type == NATPMP_RESPTYPE_TCPPORTMAPPING) ? WI_STR("TCP") : WI_STR("UDP"),
										response.pnu.newportmapping.privateport,
										response.pnu.newportmapping.mappedpublicport,
										address ? wi_address_string(address) : WI_STR("unknown address"));
						}
						
						break;
					}
				}
			}
			
			closenatpmp(&natpmp);
		}
	}
}


 
void wd_portmap_unmap_natpmp(void) {
}



#pragma mark -

void wd_portmap_map_upnp(void) {
	struct UPNPDev		*devlist, *dev;
	char				internaladdress[16], externaladdress[16], port[6];
	wi_uinteger_t		i;

	devlist = upnpDiscover(2000, NULL, NULL, 1);

	if(devlist) {
		dev = devlist;

		while(dev) {
			if(strstr(dev->st, "InternetGatewayDevice"))
				break;

			dev = dev->pNext;
		}

		if(!dev)
			dev = devlist;

		UPNP_GetValidIGD(dev,
						 &wd_portmap_upnp_urls,
						 &wd_portmap_upnp_data,
						 internaladdress,
						 sizeof(externaladdress));

		freeUPNPDevlist(devlist);
	}

	if(strlen(wd_portmap_upnp_urls.controlURL) > 0) {
		if(UPNP_GetExternalIPAddress(wd_portmap_upnp_urls.controlURL,
									 wd_portmap_upnp_data.servicetype,
									 externaladdress) != 0) {
			snprintf(externaladdress, sizeof(externaladdress), "unknown address");
		}

		snprintf(port, sizeof(port), "%u", wd_port);

		for(i = 0; i < 2; i++) {
			if(UPNP_AddPortMapping(wd_portmap_upnp_urls.controlURL,
								   wd_portmap_upnp_data.servicetype,
								   port,
								   port,
								   internaladdress,
								   "Wired",
								   (i == 0) ? "TCP" : "UDP",
								   "") == 0) {
				wi_log_info(WI_STR("Mapped internal %@ port %u to external port %u on %s using UPnP"),
					(i == 0) ? WI_STR("TCP") : WI_STR("UDP"),
					wd_port,
					wd_port,
					externaladdress);
			}
		}
	}
}



void wd_portmap_unmap_upnp(void) {
	char				port[6];
	wi_uinteger_t		i;

	if(strlen(wd_portmap_upnp_urls.controlURL) > 0) {
		snprintf(port, sizeof(port), "%u", wd_port);

		for(i = 0; i < 2; i++) {
			if(UPNP_DeletePortMapping(wd_portmap_upnp_urls.controlURL,
									  wd_portmap_upnp_data.servicetype,
									  port,
									  (i == 0) ? "TCP" : "UDP",
									  "") == 0) {
				wi_log_info(WI_STR("Unmapped %@ port %u using UPnP"),
					(i == 0) ? WI_STR("TCP") : WI_STR("UDP"),
					wd_port);
			}
		}
	}
}
