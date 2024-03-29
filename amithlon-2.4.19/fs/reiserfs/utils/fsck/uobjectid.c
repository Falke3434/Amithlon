/*
 * Copyright 1996, 1997 Hans Reiser
 */

/*#include <stdio.h>
#include <string.h>*/
/*#include <asm/bitops.h>
#include "../include/reiserfs_fs.h"
#include "../include/reiserfs_fs_sb.h"
#include "../include/reiserfslib.h"*/
#include "fsck.h"


void mark_objectid_as_used (unsigned long objectid)
{
  unsigned long * objectid_map;
  

  objectid_map = (unsigned long *)(SB_DISK_SUPER_BLOCK (&g_sb) + 1);
  if (objectid >= objectid_map[1]) {
    objectid_map[1] = objectid + 1;
  }

}


#if 0



int is_objectid_used (unsigned long objectid)
{
  unsigned long * objectid_map;
  int i = 0;

  objectid_map = (unsigned long *)(SB_DISK_SUPER_BLOCK (&g_sb) + 1);

  while (i < SB_DISK_SUPER_BLOCK (&g_sb)->s_oid_cursize) {
    if (objectid == objectid_map[i]) {
      return 1;      /* objectid is used */
    }
    
    if (objectid > objectid_map[i] && objectid < objectid_map[i+1]) {
      return 1;	/* objectid is used */
    }

    if (objectid < objectid_map[i])
      break;

    i += 2;
  }
  
  /* objectid is free */
  return 0;
}


/* we mark objectid as used. Additionally, some unused objectids can
   become used. It is ok. What is unacceptable, it is when used
   objectids are marked as unused */
void mark_objectid_as_used (unsigned long objectid)
{
  int i;
  unsigned long * objectid_map;

  if (is_objectid_used (objectid) == 1) {
    
    /*print_objectid_map (&g_sb);*/
    /*printf ("mark_objectid_as_used: objectid %lu is used", objectid);*/
    return;
  }

  objectid_map = (unsigned long *)(SB_DISK_SUPER_BLOCK (&g_sb) + 1);

  for (i = 0; i < SB_DISK_SUPER_BLOCK (&g_sb)->s_oid_cursize; i += 2) {
    if (objectid >= objectid_map [i] && objectid < objectid_map [i + 1])
      /* it is used */
      return;

    if (objectid + 1 == objectid_map[i]) {
      /* size of objectid map is the same */
      objectid_map[i] = objectid;
      return;
    }

    if (objectid == objectid_map[i + 1]) {
      /* size of objectid map is decreased */
      objectid_map[i + 1] ++;
      if (i + 2 < SB_DISK_SUPER_BLOCK (&g_sb)->s_oid_cursize) {
	if (objectid_map[i + 1] == objectid_map[i + 2]) {
	  memmove (objectid_map + i + 1, objectid_map + i + 1 + 2, 
		   (SB_DISK_SUPER_BLOCK (&g_sb)->s_oid_cursize - (i + 2 + 2 - 1)) * sizeof (unsigned long));
	  SB_DISK_SUPER_BLOCK (&g_sb)->s_oid_cursize -= 2;
	}
      }
      return;
    }
    
    if (objectid < objectid_map[i]) {
      /* size of objectid map must be increased */
      if (SB_DISK_SUPER_BLOCK (&g_sb)->s_oid_cursize == SB_DISK_SUPER_BLOCK (&g_sb)->s_oid_maxsize) {
	/* here all objectids between objectid and objectid_map[i] get used */
	objectid_map[i] = objectid;
	return;
      } else {
	memmove (objectid_map + i + 2, objectid_map + i, (SB_DISK_SUPER_BLOCK (&g_sb)->s_oid_cursize - i) * sizeof (unsigned long));
	SB_DISK_SUPER_BLOCK (&g_sb)->s_oid_cursize += 2;
      }
      
      objectid_map[i] = objectid;
      objectid_map[i+1] = objectid + 1;
      return;
    }

  }
  
  /* write out of current objectid map, if we have space */
  if (i < SB_DISK_SUPER_BLOCK (&g_sb)->s_oid_maxsize) {
    objectid_map[i] = objectid;
    objectid_map[i + 1] = objectid + 1;
    SB_DISK_SUPER_BLOCK (&g_sb)->s_oid_cursize += 2;
  } else if (i == SB_DISK_SUPER_BLOCK (&g_sb)->s_oid_maxsize) {
    objectid_map[i - 1] = objectid + 1;
  } else
    die ("mark_objectid_as_used: objectid map corrupted");
  
  return;
}


void mark_objectid_as_free (unsigned long objectid)
{
  unsigned long * oids; /* pointer to objectid map */
  int i = 0;

  oids = (unsigned long *)(SB_DISK_SUPER_BLOCK (&g_sb) + 1);

  while (i < SB_DISK_SUPER_BLOCK (&g_sb)->s_oid_cursize)
    {
      if (objectid == oids[i])
	{
	  if (i == 0)
	    die ("mark_objectid_as_free: trying to free root object id");
	  oids[i]++;

	  if (oids[i] == oids[i+1])
	    {
	      /* shrink objectid map */
	      if (SB_DISK_SUPER_BLOCK (&g_sb)->s_oid_cursize < i + 2)
		die ("mark_objectid_as_free: bad cur size");

	      memmove (oids + i, oids + i + 2, (SB_DISK_SUPER_BLOCK (&g_sb)->s_oid_cursize - i - 2) * sizeof (unsigned long));
	      SB_DISK_SUPER_BLOCK (&g_sb)->s_oid_cursize -= 2;
	      if (SB_DISK_SUPER_BLOCK (&g_sb)->s_oid_cursize < 2 || SB_DISK_SUPER_BLOCK (&g_sb)->s_oid_cursize > SB_DISK_SUPER_BLOCK (&g_sb)->s_oid_maxsize)
		die("mark_objectid_as_free: bad cur size");
	    }
	  return;
	}

      if (objectid > oids[i] && objectid < oids[i+1])
	{
	  /* size of objectid map is not changed */
	  if (objectid + 1 == oids[i+1])
	    {
	      oids[i+1]--;
	      return;
	    }

	  if (SB_DISK_SUPER_BLOCK (&g_sb)->s_oid_cursize == SB_DISK_SUPER_BLOCK (&g_sb)->s_oid_maxsize)
	    /* objectid map must be expanded, but there is no space */
	    return;

	  /* expand the objectid map*/
	  memmove (oids + i + 3, oids + i + 1, (SB_DISK_SUPER_BLOCK (&g_sb)->s_oid_cursize - i - 1) * sizeof (unsigned long));
	  oids[i+1] = objectid;
	  oids[i+2] = objectid + 1;
	  SB_DISK_SUPER_BLOCK (&g_sb)->s_oid_cursize += 2;
	  if (SB_DISK_SUPER_BLOCK (&g_sb)->s_oid_cursize < 2 || SB_DISK_SUPER_BLOCK (&g_sb)->s_oid_cursize > SB_DISK_SUPER_BLOCK (&g_sb)->s_oid_maxsize)
	    die ("objectid_release: bad cur size");
	  return;
	}
      i += 2;
    }

  die ("objectid_release: trying to free free object id (%lu)", objectid);
}

#endif
