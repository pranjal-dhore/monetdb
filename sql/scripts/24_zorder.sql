-- The contents of this file are subject to the MonetDB Public License
-- Version 1.1 (the "License"); you may not use this file except in
-- compliance with the License. You may obtain a copy of the License at
-- http://www.monetdb.org/Legal/MonetDBLicense
--
-- Software distributed under the License is distributed on an "AS IS"
-- basis, WITHOUT WARRANTY OF ANY KIND, either express or implied. See the
-- License for the specific language governing rights and limitations
-- under the License.
--
-- The Original Code is the MonetDB Database System.
--
-- The Initial Developer of the Original Code is CWI.
-- Copyright August 2008-2013 MonetDB B.V.
-- All Rights Reserved.

create function zorder_encode(x integer, y integer) returns oid
    external name zorder.encode;

create function zorder_decode_x(z oid) returns integer
    external name zorder.decode_x;

create function zorder_decode_y(z oid) returns integer
    external name zorder.decode_y;
