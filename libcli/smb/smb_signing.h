/*
   Unix SMB/CIFS implementation.
   SMB Signing Code
   Copyright (C) Jeremy Allison 2003.
   Copyright (C) Andrew Bartlett <abartlet@samba.org> 2002-2003
   Copyright (C) Stefan Metzmacher 2009

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#ifndef _SMB_SIGNING_H_
#define _SMB_SIGNING_H_

struct smb1_signing_state;

struct smb1_signing_state *smb1_signing_init(TALLOC_CTX *mem_ctx,
					   bool allowed,
					   bool desired,
					   bool mandatory);
struct smb1_signing_state *smb1_signing_init_ex(TALLOC_CTX *mem_ctx,
					      bool allowed,
					      bool desired,
					      bool mandatory,
					      void *(*alloc_fn)(TALLOC_CTX *, size_t),
					      void (*free_fn)(TALLOC_CTX *, void *));
uint32_t smb1_signing_next_seqnum(struct smb1_signing_state *si, bool oneway);
void smb1_signing_cancel_reply(struct smb1_signing_state *si, bool oneway);
NTSTATUS smb1_signing_sign_pdu(struct smb1_signing_state *si,
			      uint8_t *outhdr, size_t len,
			      uint32_t seqnum);
bool smb1_signing_check_pdu(struct smb1_signing_state *si,
			   const uint8_t *inhdr, size_t len,
			   uint32_t seqnum);
bool smb1_signing_activate(struct smb1_signing_state *si,
			  const DATA_BLOB user_session_key,
			  const DATA_BLOB response);
bool smb1_signing_is_active(struct smb1_signing_state *si);
bool smb1_signing_is_desired(struct smb1_signing_state *si);
bool smb1_signing_is_mandatory(struct smb1_signing_state *si);
bool smb1_signing_set_negotiated(struct smb1_signing_state *si,
				bool allowed, bool mandatory);
bool smb1_signing_is_negotiated(struct smb1_signing_state *si);
NTSTATUS smb1_key_derivation(const uint8_t *KI,
			    size_t KI_len,
			    uint8_t KO[16]);

#endif /* _SMB_SIGNING_H_ */
