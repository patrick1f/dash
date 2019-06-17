#include <string.h>

#include <dash/dart/gaspi/dart_gaspi.h>
#include <dash/dart/gaspi/dart_team_private.h>
#include <dash/dart/gaspi/dart_translation.h>
#include <dash/dart/gaspi/dart_mem.h>
#include <dash/dart/gaspi/dart_communication_priv.h>


dart_ret_t dart_scatter(
   const void        * sendbuf,
   void              * recvbuf,
   size_t              nelem,
   dart_datatype_t     dtype,
   dart_team_unit_t    root,
   dart_team_t         teamid)
{

    dart_datatype_struct_t* dts = get_datatype_struct(dtype);
    if(!datatype_isbasic(dts))
    {
      DART_LOG_ERROR("dart_scatter: complex datatypes are not supported!");

      return DART_ERR_INVAL;
    }
    size_t nbytes_elem = datatype_sizeof(dts);
    size_t nbytes = nbytes_elem * nelem;

    uint16_t index;
    if(dart_adapt_teamlist_convert(teamid, &index) == -1)
    {
        DART_LOG_ERROR("dart_scatter: no team with id: %d\n", teamid);
        return DART_ERR_OTHER;
    }

    dart_team_unit_t myid;
    DART_CHECK_ERROR(dart_team_myid(teamid, &myid));

    size_t team_size;
    DART_CHECK_ERROR(dart_team_size(teamid, &team_size));

    gaspi_queue_id_t queue;
    DART_CHECK_ERROR( dart_get_minimal_queue(&queue));

    DART_CHECK_GASPI_ERROR(gaspi_segment_use(dart_coll_seg, recvbuf, nbytes, dart_teams[index].id, GASPI_BLOCK, 0));

    gaspi_notification_t notify_value = 42;
    gaspi_notification_id_t notify_id = 0;
    if(myid.id == root.id)
    {
        DART_CHECK_GASPI_ERROR(gaspi_segment_bind(dart_onesided_seg, (void* const) sendbuf, nbytes * team_size, 0));
        DART_LOG_DEBUG("dart_scatter: starting to scatter for team: %d size: %d ", teamid, team_size);
        for(gaspi_rank_t unit_id = 0; unit_id < team_size; ++unit_id)
        {
            if(myid.id == unit_id) continue;

            dart_unit_t glob_unit_id;
            DART_CHECK_ERROR(unit_l2g(index, &glob_unit_id, unit_id));

            DART_CHECK_GASPI_ERROR(gaspi_write_notify(dart_onesided_seg,
                                                      unit_id * nbytes,
                                                      glob_unit_id,
                                                      dart_coll_seg,
                                                      0,
                                                      nbytes,
                                                      notify_id,
                                                      notify_value,
                                                      queue,
                                                      GASPI_BLOCK));
        }

        memcpy(recvbuf, (char*) sendbuf + myid.id * nbytes, nbytes);

        DART_CHECK_GASPI_ERROR(gaspi_segment_delete(dart_onesided_seg));
    }
    else
    {
        gaspi_notification_id_t first_id;
        gaspi_notification_t    old_value;
        DART_LOG_TRACE("dart_scatter: waiting to receive from scatter root %d", root.id);
        DART_CHECK_GASPI_ERROR(gaspi_notify_waitsome(dart_coll_seg, notify_id, 1, &first_id, GASPI_BLOCK));
        DART_CHECK_GASPI_ERROR(gaspi_notify_reset(dart_coll_seg, first_id, &old_value));

        if(old_value != notify_value)
        {
            DART_LOG_ERROR("dart_scatter: Error in process synchronization -> wrong notification value");
            return DART_ERR_OTHER;
        }
    }

    DART_CHECK_GASPI_ERROR(gaspi_segment_delete(dart_coll_seg));

    DART_CHECK_ERROR(dart_barrier(teamid));

    return DART_OK;
}

/**
 * Implemented a binominal tree to broadcast the data
 */
dart_ret_t dart_bcast(
   void              * buf,
   size_t              nelem,
   dart_datatype_t     dtype,
   dart_team_unit_t    root,
   dart_team_t         teamid
)
{

    dart_datatype_struct_t* dts = get_datatype_struct(dtype);
    if(!datatype_isbasic(dts))
    {
      DART_LOG_ERROR("dart_bcast: complex datatypes are not supported!");

      return DART_ERR_INVAL;
    }
    size_t nbytes_elem = datatype_sizeof(dts);
    size_t nbytes = nbytes_elem * nelem;

    uint16_t index;
    if(dart_adapt_teamlist_convert(teamid, &index) == -1)
    {
        DART_LOG_ERROR("dart_bcast: no team with id: %d", teamid);
        return DART_ERR_OTHER;
    }

    dart_team_unit_t myid;
    DART_CHECK_ERROR(dart_team_myid(teamid, &myid));

    size_t team_size;
    DART_CHECK_ERROR(dart_team_size(teamid, &team_size));

    gaspi_queue_id_t queue;
    DART_CHECK_ERROR( dart_get_minimal_queue(&queue));

    DART_CHECK_GASPI_ERROR(gaspi_segment_use(dart_coll_seg, buf, nbytes, dart_teams[index].id, GASPI_BLOCK, 0));

    int  parent;
    int* children = NULL;
    int  children_count = gaspi_utils_compute_comms(&parent, &children, myid.id, root.id, team_size);

    gaspi_notification_t notify_value = 42;
    gaspi_notification_id_t notify_id = 0;
    if (myid.id != parent)
    {
        DART_LOG_TRACE("dart_bcast: waiting for bcast as child");
        gaspi_notification_id_t first_id;
        gaspi_notification_t    old_value;
        DART_CHECK_GASPI_ERROR(gaspi_notify_waitsome(dart_coll_seg, notify_id, 1, &first_id, GASPI_BLOCK));
        DART_CHECK_GASPI_ERROR(gaspi_notify_reset(dart_coll_seg, first_id, &old_value));

        if(old_value != notify_value)
        {
            DART_LOG_ERROR("dart_bcast: Error in process synchronization -> wrong notification value");
            return DART_ERR_OTHER;
        }
    }

    DART_LOG_TRACE("dart_bcast: as parent start write_notify to all %d waiting children", children_count);
    for (int child = 0; child < children_count; child++)
    {
        dart_unit_t glob_unit_id;
        DART_CHECK_ERROR(unit_l2g(index, &glob_unit_id, children[child]));

        DART_CHECK_GASPI_ERROR(gaspi_write_notify(dart_coll_seg,
                                                  0,
                                                  glob_unit_id,
                                                  dart_coll_seg,
                                                  0,
                                                  nbytes,
                                                  notify_id,
                                                  notify_value,
                                                  queue,
                                                  GASPI_BLOCK));
    }


    DART_CHECK_GASPI_ERROR(gaspi_segment_delete(dart_coll_seg));

    DART_CHECK_ERROR(dart_barrier(teamid));

    return DART_OK;
}

dart_ret_t dart_gather(
      const void         * sendbuf,
      void               * recvbuf,
      size_t               nelem,
      dart_datatype_t      dtype,
      dart_team_unit_t     root,
      dart_team_t          teamid)
{
    dart_datatype_struct_t* dts = get_datatype_struct(dtype);
    if(!datatype_isbasic(dts))
    {
      DART_LOG_ERROR("dart_gather: complex datatypes are not supported!");
      return DART_ERR_INVAL;
    }
    size_t nbytes_elem = datatype_sizeof(dts);
    size_t nbytes = nbytes_elem * nelem;

    uint16_t index;
    if(dart_adapt_teamlist_convert(teamid, &index) == -1)
    {
        DART_LOG_ERROR("dart_gather: no team with id: %d\n", teamid);
        return DART_ERR_OTHER;
    }

    dart_team_unit_t myid;
    DART_CHECK_ERROR(dart_team_myid(teamid, &myid));

    size_t team_size;
    DART_CHECK_ERROR(dart_team_size(teamid, &team_size));

    gaspi_queue_id_t queue;
    DART_CHECK_ERROR( dart_get_minimal_queue(&queue));

    DART_LOG_DEBUG("dart_gather: convert local to global id from all members of team: %d and register on dart_coll_seg: %d", teamid, dart_coll_seg);
    if(myid.id == root.id)
    {
      DART_CHECK_GASPI_ERROR(gaspi_segment_bind(dart_coll_seg, recvbuf, nbytes * team_size, 0));
      for(int i = 0; i < team_size; ++i)
      {
        dart_unit_t glob_unit_id;
        DART_CHECK_ERROR(unit_l2g(index, &glob_unit_id, i));
        DART_CHECK_GASPI_ERROR(gaspi_segment_register(dart_coll_seg, glob_unit_id, GASPI_BLOCK));
      }
    }
    DART_CHECK_ERROR(dart_barrier(teamid));

    gaspi_notification_t notify_value = 42;

    if(myid.id != root.id)
    {
        DART_LOG_DEBUG("dart_gather: write_notify from non root");
        // guarantees contiguous notification ids
        gaspi_notification_id_t notify_id = (myid.id < root.id) ? myid.id : myid.id - 1;
        dart_unit_t glob_root_id;
        DART_CHECK_ERROR(unit_l2g(index, &glob_root_id, root.id));
        DART_CHECK_GASPI_ERROR(gaspi_segment_bind(dart_onesided_seg, (void* const) sendbuf, nbytes, 0));
        //Todo: error handling
        DART_CHECK_GASPI_ERROR(gaspi_write_notify(dart_onesided_seg,
                                                  0UL,
                                                  glob_root_id,
                                                  dart_coll_seg,
                                                  myid.id * nbytes,
                                                  nbytes,
                                                  notify_id,
                                                  notify_value,
                                                  queue,
                                                  GASPI_BLOCK));

        DART_CHECK_GASPI_ERROR(gaspi_segment_delete(dart_onesided_seg));
    }
    else
    {
        DART_LOG_TRACE("dart_gather: wait for write as root as long as notifies are left");
        memcpy((char*) recvbuf + myid.id * nbytes, sendbuf, nbytes);

        gaspi_notification_id_t first_id;
        gaspi_notification_t    old_value;

        int notifies_left = team_size - 1;
        while(notifies_left > 0)
        {
            DART_CHECK_GASPI_ERROR(blocking_waitsome(dart_coll_seg, 0, team_size - 1, &first_id, &old_value));
            if(old_value != notify_value)
            {
                DART_LOG_ERROR("dart_gather: Error in process synchronization -> wrong notification value");
                return DART_ERR_OTHER;
            }
            --notifies_left;
        }

        DART_CHECK_GASPI_ERROR(gaspi_segment_delete(dart_coll_seg));
    }

    DART_CHECK_ERROR(dart_barrier(teamid));

    return DART_OK;
}

dart_ret_t dart_allgather(
  const void      * sendbuf,
  void            * recvbuf,
  size_t            nelem,
  dart_datatype_t   dtype,
  dart_team_t       teamid)
{
    dart_datatype_struct_t* dts = get_datatype_struct(dtype);
    if(!datatype_isbasic(dts))
    {
      DART_LOG_ERROR("dart_allgather: complex datatypes are not supported!");

      return DART_ERR_INVAL;
    }
    size_t nbytes_elem = datatype_sizeof(dts);
    size_t nbytes = nbytes_elem * nelem;

    uint16_t index;
    if(dart_adapt_teamlist_convert(teamid, &index) == -1)
    {
        DART_LOG_ERROR("dart_allgather: no team with id: %d", teamid);
        return DART_ERR_OTHER;
    }

    dart_team_unit_t myid;
    DART_CHECK_ERROR(dart_team_myid(teamid, &myid));

    size_t team_size;
    DART_CHECK_ERROR(dart_team_size(teamid, &team_size));

    gaspi_queue_id_t queue;
    DART_CHECK_ERROR( dart_get_minimal_queue(&queue));

    DART_CHECK_GASPI_ERROR(gaspi_segment_use(dart_coll_seg, recvbuf, nbytes * team_size, dart_teams[index].id, GASPI_BLOCK, 0));

    DART_CHECK_GASPI_ERROR(gaspi_segment_bind(dart_onesided_seg, (void* const) sendbuf, nbytes, 0));

    gaspi_notification_t notify_value = 42;
    gaspi_notification_id_t notify_id = myid.id;
    gaspi_offset_t remote_offset = myid.id * nbytes;

    DART_LOG_DEBUG("dart_allgather: every rank write_notify to all other ranks in team: %d", teamid);
    for(gaspi_rank_t unit_id = 0; unit_id < team_size; ++unit_id)
    {
        if(unit_id == myid.id) continue;

        dart_unit_t glob_unit_id;
        DART_CHECK_ERROR(unit_l2g(index, &glob_unit_id, unit_id));


        DART_CHECK_GASPI_ERROR(gaspi_write_notify(dart_onesided_seg,
                                                  0UL,
                                                  glob_unit_id,
                                                  dart_coll_seg,
                                                  remote_offset,
                                                  nbytes,
                                                  notify_id,
                                                  notify_value,
                                                  queue,
                                                  GASPI_BLOCK));
    }

    memcpy((char*) recvbuf + remote_offset, sendbuf, nbytes);

    gaspi_notification_id_t found_id;
    gaspi_notification_t    old_value;

    int notifies_left = team_size - 1;
    DART_LOG_TRACE("dart_allgather: wait for all writes that have notifies left");
    while(notifies_left > 0)
    {
        DART_CHECK_GASPI_ERROR(gaspi_notify_waitsome(dart_coll_seg, 0, team_size,
                           &found_id, GASPI_BLOCK));
        DART_CHECK_GASPI_ERROR(gaspi_notify_reset(dart_coll_seg, found_id, &old_value));
        //shouldn't happen
        if(found_id == myid.id)
        {
            DART_LOG_ERROR("dart_allgather: Error in process synchronization -> notify id for local unit");
            continue;
        }

        if(old_value != notify_value)
        {
            DART_LOG_ERROR("dart_allgather: Error in process synchronization -> wrong notification value");
            return DART_ERR_OTHER;
        }
        --notifies_left;
    }

    DART_CHECK_ERROR(dart_barrier(teamid));
    DART_CHECK_GASPI_ERROR(gaspi_segment_delete(dart_onesided_seg));
    DART_CHECK_GASPI_ERROR(gaspi_segment_delete(dart_coll_seg));

    return DART_OK;
}


//slightly altered version of gaspi dart_allgather
dart_ret_t dart_allgatherv(
  const void      * sendbuf,
  size_t            nsendelems,
  dart_datatype_t   dtype,
  void            * recvbuf,
  const size_t    * nrecvelems,
  const size_t    * recvdispls,
  dart_team_t       teamid)
{
    dart_datatype_struct_t* dts = get_datatype_struct(dtype);
    if(!datatype_isbasic(dts))
    {
      DART_LOG_ERROR("dart_allgatherv: complex datatypes are not supported!");

      return DART_ERR_INVAL;
    }

    uint16_t index;
    if(dart_adapt_teamlist_convert(teamid, &index) == -1)
    {
        DART_LOG_ERROR("dart_allgatherv: no team with id: %d\n", teamid);
        return DART_ERR_OTHER;
    }

    dart_team_unit_t myid;
    DART_CHECK_ERROR(dart_team_myid(teamid, &myid));
    dart_global_unit_t glob_myid;
    DART_CHECK_ERROR(dart_myid(&glob_myid));

    size_t team_size;
    DART_CHECK_ERROR(dart_team_size(teamid, &team_size));

    gaspi_queue_id_t queue;
    DART_CHECK_ERROR( dart_get_minimal_queue(&queue));

    size_t last_index = team_size-1;
    size_t nbytes_elem = datatype_sizeof(dts);
    size_t num_total_elemnts = recvdispls[last_index] + nrecvelems[last_index];
    size_t nbytes_recv = nbytes_elem * num_total_elemnts;
    size_t nbytes_send = nbytes_elem * nsendelems;

    DART_CHECK_GASPI_ERROR(gaspi_segment_use(dart_coll_seg, recvbuf, nbytes_recv, dart_teams[index].id, GASPI_BLOCK, 0));

    DART_CHECK_GASPI_ERROR(gaspi_segment_bind(dart_onesided_seg, (void* const) sendbuf, nbytes_send, 0));

    gaspi_notification_t notify_value = 42;
    gaspi_notification_id_t notify_id = myid.id;
    gaspi_offset_t remote_offset = recvdispls[myid.id] * nbytes_elem;

    DART_LOG_DEBUG("dart_allgatherv: every rank write_notify to all other ranks in team: %d", teamid);
    for(gaspi_rank_t unit_id = 0; unit_id < team_size; ++unit_id)
    {
        if(unit_id == myid.id) continue;

        dart_unit_t glob_unit_id;
        DART_CHECK_ERROR(unit_l2g(index, &glob_unit_id, unit_id));

        DART_CHECK_GASPI_ERROR(gaspi_write_notify(dart_onesided_seg,
                                                  0UL,
                                                  glob_unit_id,
                                                  dart_coll_seg,
                                                  remote_offset,
                                                  nbytes_send,
                                                  notify_id,
                                                  notify_value,
                                                  queue,
                                                  GASPI_BLOCK));
    }

    memcpy((char*) recvbuf + remote_offset, sendbuf, nbytes_send);

    gaspi_notification_id_t found_id;
    gaspi_notification_t    old_value;

    int notifies_left = team_size - 1;
    DART_LOG_TRACE("dart_allgatherv: wait for all writes that have notifies left");
    while(notifies_left > 0)
    {
        DART_CHECK_GASPI_ERROR(gaspi_notify_waitsome(dart_coll_seg, 0, team_size,
                           &found_id, GASPI_BLOCK));
        DART_CHECK_GASPI_ERROR(gaspi_notify_reset(dart_coll_seg, found_id, &old_value));
        //shouldn't happen
        if(found_id == myid.id)
        {
          DART_LOG_ERROR("dart_allgatherv: Error in process synchronization -> notify id for local unit");
          continue;
        }

        if(old_value != notify_value)
        {
            DART_LOG_ERROR("dart_allgatherv: Error in process synchronization -> wrong notification value");
            return DART_ERR_OTHER;
        }
        --notifies_left;
    }

    DART_CHECK_ERROR(dart_barrier(teamid));
    DART_CHECK_GASPI_ERROR(gaspi_segment_delete(dart_onesided_seg));
    DART_CHECK_GASPI_ERROR(gaspi_segment_delete(dart_coll_seg));

    return DART_OK;
}

dart_ret_t dart_alltoall(
    const void *    sendbuf,
    void *          recvbuf,
    size_t          nelem,
    dart_datatype_t dtype,
    dart_team_t     teamid)
{
        dart_datatype_struct_t* dts = get_datatype_struct(dtype);
    if(!datatype_isbasic(dts))
    {
      DART_LOG_ERROR("dart_alltoall: complex datatypes are not supported!");

      return DART_ERR_INVAL;
    }
    size_t nbytes_elem = datatype_sizeof(dts);
    size_t nbytes = nbytes_elem * nelem;

    uint16_t index;
    if(dart_adapt_teamlist_convert(teamid, &index) == -1)
    {
        DART_LOG_ERROR("dart_alltoall: no team with id: %d\n", teamid);
        return DART_ERR_OTHER;
    }

    dart_team_unit_t myid;
    DART_CHECK_ERROR(dart_team_myid(teamid, &myid));

    size_t team_size;
    DART_CHECK_ERROR(dart_team_size(teamid, &team_size));

    gaspi_queue_id_t queue;
    DART_CHECK_ERROR( dart_get_minimal_queue(&queue));

    DART_CHECK_GASPI_ERROR(gaspi_segment_use(dart_coll_seg, recvbuf, nbytes * team_size, dart_teams[index].id, GASPI_BLOCK, 0));

    DART_CHECK_GASPI_ERROR(gaspi_segment_bind(dart_onesided_seg, (void* const) sendbuf, nbytes * team_size, 0));

    gaspi_notification_t notify_value = 42;
    gaspi_notification_id_t notify_id = myid.id;
    gaspi_offset_t remote_offset = myid.id * nbytes;

    DART_LOG_DEBUG("dart_alltoall: every rank write_notify to all other ranks in team: %d", teamid);
    for(gaspi_rank_t unit_id = 0; unit_id < team_size; ++unit_id)
    {
        if(unit_id == myid.id) continue;

        dart_unit_t glob_unit_id;
        DART_CHECK_ERROR(unit_l2g(index, &glob_unit_id, unit_id));


        DART_CHECK_GASPI_ERROR(gaspi_write_notify(dart_onesided_seg,
                                                  unit_id * nbytes,
                                                  glob_unit_id,
                                                  dart_coll_seg,
                                                  remote_offset,
                                                  nbytes,
                                                  notify_id,
                                                  notify_value,
                                                  queue,
                                                  GASPI_BLOCK));
    }

    memcpy((char*) recvbuf + remote_offset, sendbuf + remote_offset, nbytes);

    gaspi_notification_id_t found_id;
    gaspi_notification_t    old_value;

    int notifies_left = team_size - 1;
    DART_LOG_TRACE("dart_alltoall: wait for all writes that have notifies left");
    while(notifies_left > 0)
    {
        DART_CHECK_GASPI_ERROR(gaspi_notify_waitsome(dart_coll_seg, 0, team_size,
                           &found_id, GASPI_BLOCK));
        DART_CHECK_GASPI_ERROR(gaspi_notify_reset(dart_coll_seg, found_id, &old_value));
        //shouldn't happen
        if(found_id == myid.id)
        {
          DART_LOG_ERROR("dart_alltoall: Error in process synchronization -> notify id for local unit");
          continue;
        }

        if(old_value != notify_value)
        {
            DART_LOG_ERROR("dart_alltoall: Error in process synchronization -> wrong notification value");
            return DART_ERR_OTHER;
        }
        --notifies_left;
    }

    DART_CHECK_ERROR(dart_barrier(teamid));
    DART_CHECK_GASPI_ERROR(gaspi_segment_delete(dart_onesided_seg));
    DART_CHECK_GASPI_ERROR(gaspi_segment_delete(dart_coll_seg));

    return DART_OK;
}

dart_ret_t dart_barrier (
  dart_team_t teamid)
{
    uint16_t       index;
    if (dart_adapt_teamlist_convert (teamid, &index) == -1)
    {
        return DART_ERR_INVAL;
    }
    gaspi_group_t gaspi_group_id = dart_teams[index].id;
    DART_CHECK_GASPI_ERROR(gaspi_barrier(gaspi_group_id, GASPI_BLOCK));

    return DART_OK;
}


dart_ret_t dart_get_blocking(
  void            * dst,
  dart_gptr_t       gptr,
  size_t            nelem,
  dart_datatype_t   src_type,
  dart_datatype_t   dst_type)
{
    dart_datatype_struct_t* dts_src = get_datatype_struct(src_type);
    dart_datatype_struct_t* dts_dst = get_datatype_struct(dst_type);
    CHECK_EQUAL_BASETYPE(dts_src, dts_dst);

    // initialized with relative team unit id
    dart_unit_t global_src_unit_id = gptr.unitid;

    gaspi_segment_id_t gaspi_src_seg_id = 0;
    DART_CHECK_ERROR(glob_unit_gaspi_seg(&gptr, &global_src_unit_id, &gaspi_src_seg_id, "dart_get_blocking"));

    dart_global_unit_t global_myid;
    DART_CHECK_ERROR(dart_myid(&global_myid));

    converted_type_t conv_type;
    DART_CHECK_ERROR(dart_convert_type(dts_src, dts_dst, nelem, &conv_type));

    DART_LOG_DEBUG("dart_get_blocking: blocking get with dest_seg: %d, own_unit_id: %d, conv_type kind: %d", global_src_unit_id, global_myid.id, conv_type.kind);
    if(global_myid.id == global_src_unit_id)
    {
        DART_LOG_TRACE("dart_get_blocking: local blocking get");
        DART_CHECK_GASPI_ERROR_CLEAN(conv_type,
            local_get(&gptr, gaspi_src_seg_id, dst, &conv_type));
    }
    else
    {
        DART_LOG_TRACE("dart_get_blocking: start remote get");
        gaspi_queue_id_t queue = (gaspi_queue_id_t) -1;

        DART_CHECK_GASPI_ERROR_CLEAN_SEG(dart_onesided_seg, conv_type,
          remote_get(&gptr,
                    global_src_unit_id,
                    gaspi_src_seg_id,
                    dart_onesided_seg,
                    dst,
                    &queue,
                    &conv_type)
        );

        DART_CHECK_GASPI_ERROR_CLEAN_SEG(dart_onesided_seg, conv_type,
            gaspi_wait(queue, GASPI_BLOCK));
        DART_LOG_TRACE("dart_get_blocking: finished waiting for remote get");
        DART_CHECK_GASPI_ERROR_CLEAN(conv_type,
            gaspi_segment_delete(dart_onesided_seg));
    }

    free_converted_type(&conv_type);

    return DART_OK;
}

dart_ret_t dart_put_blocking(
  dart_gptr_t       gptr,
  const void      * src,
  size_t            nelem,
  dart_datatype_t   src_type,
  dart_datatype_t   dst_type)
{
    dart_datatype_struct_t* dts_src = get_datatype_struct(src_type);
    dart_datatype_struct_t* dts_dst = get_datatype_struct(dst_type);
    CHECK_EQUAL_BASETYPE(dts_src, dts_dst);

    // initialized with relative team unit id
    dart_unit_t global_dst_unit_id = gptr.unitid;

    gaspi_segment_id_t gaspi_dst_seg_id = 0;
    DART_CHECK_ERROR(glob_unit_gaspi_seg(&gptr, &global_dst_unit_id, &gaspi_dst_seg_id, "dart_put_handle"));

    dart_global_unit_t global_myid;
    DART_CHECK_ERROR(dart_myid(&global_myid));

    converted_type_t conv_type;
    DART_CHECK_ERROR(dart_convert_type(dts_src, dts_dst, nelem, &conv_type));


    DART_LOG_DEBUG("dart_put_blocking: starting put with dest_seg: %d, own_unit_id: %d, conv_type kind: %d", gaspi_dst_seg_id, global_myid.id, conv_type.kind);
    if(global_myid.id == global_dst_unit_id)
    {
        DART_LOG_TRACE("dart_put_blocking: start local put");
        DART_CHECK_GASPI_ERROR_CLEAN(conv_type,
            local_put(&gptr, gaspi_dst_seg_id, src, &conv_type));
    }
    else
    {
        DART_LOG_TRACE("dart_put_blocking: start remote put");
        gaspi_queue_id_t queue = (gaspi_queue_id_t) -1;

        DART_CHECK_GASPI_ERROR_CLEAN_SEG(dart_onesided_seg, conv_type,
            remote_put(&gptr,
                      global_dst_unit_id,
                      gaspi_dst_seg_id,
                      dart_onesided_seg,
                      src,
                      &queue,
                      &conv_type)
        );

        DART_CHECK_GASPI_ERROR_CLEAN_SEG(dart_onesided_seg, conv_type,
            put_completion_test(global_dst_unit_id, queue));

        DART_CHECK_GASPI_ERROR_CLEAN_SEG(dart_onesided_seg, conv_type,
            gaspi_wait(queue, GASPI_BLOCK));
        DART_LOG_TRACE("dart_put_blocking: finished waiting for remote put");
        DART_CHECK_GASPI_ERROR_CLEAN(conv_type,
            gaspi_segment_delete(dart_onesided_seg));
    }

    free_converted_type(&conv_type);

    return DART_OK;
}

dart_ret_t dart_handle_free(
  dart_handle_t * handleptr)
{
    if(handleptr == NULL || *handleptr == DART_HANDLE_NULL)
    {
      DART_LOG_DEBUG("dart_handle_free: empty handle");
      return DART_OK;
    }

    dart_handle_t handle = *handleptr;

    gaspi_notification_t val = 0;
    DART_CHECK_GASPI_ERROR(gaspi_notify_reset (handle->local_seg_id, (gaspi_notification_id_t) handle->local_seg_id, &val));
    if(handle->comm_kind == GASPI_WRITE)
    {
        gaspi_notification_t val_remote = 0;
        DART_CHECK_GASPI_ERROR(gaspi_notify_reset (handle->local_seg_id, handle->notify_remote, &val_remote));
        if(val_remote != handle->notify_remote)
        {
          DART_LOG_ERROR("dart_handle_free: Error: gaspi remote completion notify value != expected value");
        }
    }

    if(val != handle->local_seg_id)
    {
      DART_LOG_ERROR("dart_handle_free: Error: gaspi notify value != expected value");
    }

    DART_LOG_DEBUG("dart_handle_free: Delete segment: %d and push that back onto stack.", handle->local_seg_id);
    DART_CHECK_GASPI_ERROR(gaspi_segment_delete(handle->local_seg_id));
    DART_CHECK_ERROR(seg_stack_push(&pool_gaspi_seg_ids, handle->local_seg_id));

    free(handle);
    *handleptr = DART_HANDLE_NULL;

    return DART_OK;
}

dart_ret_t dart_wait_local (
  dart_handle_t * handleptr)
{
    if (handleptr != NULL && *handleptr != DART_HANDLE_NULL)
    {
        DART_CHECK_GASPI_ERROR(gaspi_wait((*handleptr)->queue, GASPI_BLOCK));

        DART_CHECK_ERROR(dart_handle_free(handleptr));
    }

    return DART_OK;
}

dart_ret_t dart_waitall_local(
  dart_handle_t handles[],
  size_t        num_handles)
{
    if (handles != NULL)
    {
        for(size_t i = 0 ; i < num_handles ; ++i)
        {
            DART_CHECK_ERROR(dart_wait_local(&handles[i]));
        }
    }

    return DART_OK;
}

dart_ret_t dart_wait(
  dart_handle_t * handleptr)
{

    if( handleptr != NULL && *handleptr != DART_HANDLE_NULL )
    {
        DART_CHECK_GASPI_ERROR((gaspi_wait((*handleptr)->queue, GASPI_BLOCK)));
        dart_handle_free(handleptr);
    }

    return DART_OK;
}

dart_ret_t dart_waitall(
  dart_handle_t handles[],
  size_t        num_handles)
{
    if ( handles == NULL || num_handles == 0)
    {
        DART_LOG_DEBUG("dart_waitall: empty handles");
        return DART_OK;
    }

    for(size_t i = 0; i < num_handles; ++i)
    {
        DART_CHECK_ERROR(dart_wait(&handles[i]));
    }

    return DART_OK;
}

dart_ret_t dart_test_local (
  dart_handle_t * handleptr,
  int32_t       * is_finished)
{
    if (handleptr == NULL || *handleptr == DART_HANDLE_NULL)
    {
        *is_finished = 1;
        DART_LOG_DEBUG("dart_test_local: empty handle");

        return DART_OK;
    }

    return dart_test_impl(handleptr, is_finished, (*handleptr)->local_seg_id);
}

dart_ret_t dart_testall_local(
  dart_handle_t   handles[],
  size_t          num_handles,
  int32_t       * is_finished)
{
    if (handles == NULL || num_handles == 0)
    {
        *is_finished = 1;
        DART_LOG_DEBUG("dart_testall_local: empty handle");

        return DART_OK;
    }

    return dart_test_all_impl(handles, num_handles, is_finished, GASPI_LOCAL);
}

dart_ret_t dart_test(
  dart_handle_t * handleptr,
  int32_t       * is_finished)
{
    if(handleptr == NULL || *handleptr == DART_HANDLE_NULL)
    {
        *is_finished = 1;
        DART_LOG_DEBUG("dart_test: empty handle");

        return DART_OK;
    }

    if((*handleptr)->comm_kind == GASPI_READ)
    {
        return dart_test_impl(handleptr, is_finished, (*handleptr)->local_seg_id);
    }

    return dart_test_impl(handleptr, is_finished, (*handleptr)->notify_remote);
}

dart_ret_t dart_testall(
  dart_handle_t   handles[],
  size_t          num_handles,
  int32_t       * is_finished)
{
    if(handles == NULL || num_handles == 0)
    {
        *is_finished = 1;
        DART_LOG_DEBUG("dart_testall: empty handle");

        return DART_OK;
    }

    return dart_test_all_impl(handles, num_handles, is_finished, GASPI_GLOBAL);
}

dart_ret_t dart_get_handle(
  void*           dst,
  dart_gptr_t     gptr,
  size_t          nelem,
  dart_datatype_t src_type,
  dart_datatype_t dst_type,
  dart_handle_t*  handleptr)
{
    dart_datatype_struct_t* dts_src = get_datatype_struct(src_type);
    dart_datatype_struct_t* dts_dst = get_datatype_struct(dst_type);
    CHECK_EQUAL_BASETYPE(dts_src, dts_dst);

    *handleptr = DART_HANDLE_NULL;

    size_t nbytes_elem = datatype_sizeof(datatype_base_struct(dts_src));
    size_t nbytes_segment = nbytes_elem * nelem;

    // initialized with relative team unit id
    dart_unit_t global_src_unit_id = gptr.unitid;

    gaspi_segment_id_t gaspi_src_seg_id = 0;
    DART_CHECK_ERROR(glob_unit_gaspi_seg(&gptr, &global_src_unit_id, &gaspi_src_seg_id, "dart_get_handled"));

    dart_global_unit_t global_myid;
    DART_CHECK_ERROR(dart_myid(&global_myid));

    converted_type_t conv_type;
    DART_CHECK_ERROR(dart_convert_type(dts_src, dts_dst, nelem, &conv_type));

    DART_LOG_DEBUG("dart_get_handled: starting get with dest_seg: %d", gaspi_src_seg_id );
    if(global_myid.id == global_src_unit_id)
    {
        DART_LOG_TRACE("dart_get_handled: local get -> empty handle");
        DART_CHECK_ERROR_CLEAN(conv_type,
            local_get(&gptr, gaspi_src_seg_id, dst, &conv_type));
    }
    else
    {
        // get gaspi segment id and bind it to dst
        gaspi_segment_id_t free_seg_id;
        DART_CHECK_GASPI_ERROR_CLEAN(conv_type,
            seg_stack_pop(&pool_gaspi_seg_ids, &free_seg_id));
        DART_LOG_DEBUG("dart_get_handled: take seg_id: %d from stack as dst for remote_get", free_seg_id);
        gaspi_queue_id_t queue = (gaspi_queue_id_t) -1;

        DART_CHECK_GASPI_ERROR_CLEAN_SEG(free_seg_id, conv_type,
            remote_get(&gptr,
                      global_src_unit_id,
                      gaspi_src_seg_id,
                      free_seg_id,
                      dst,
                      &queue,
                      &conv_type)
        );

        DART_CHECK_GASPI_ERROR_CLEAN_SEG(free_seg_id, conv_type,
            gaspi_notify(free_seg_id, global_myid.id, free_seg_id, free_seg_id, queue, GASPI_BLOCK));

        dart_handle_t handle = (dart_handle_t) malloc(sizeof(*handle));

        handle->comm_kind     = GASPI_READ;
        handle->queue         = queue;
        handle->local_seg_id  = free_seg_id;
        handle->notify_remote = 0;
        *handleptr = handle;

        DART_LOG_DEBUG("dart_get_handled: finished remote get and constructed handle[GASPI_READ, %d, %d, 0]", queue, free_seg_id);
    }

    free_converted_type(&conv_type);

    DART_LOG_DEBUG("dart_get_handle: handle(%p) dest:%d\n", (void*)(*handleptr), global_src_unit_id);

    return DART_OK;
}



dart_ret_t dart_put_handle(
  dart_gptr_t       gptr,
  const void      * src,
  size_t            nelem,
  dart_datatype_t   src_type,
  dart_datatype_t   dst_type,
  dart_handle_t   * handleptr)
{
    dart_datatype_struct_t* dts_src = get_datatype_struct(src_type);
    dart_datatype_struct_t* dts_dst = get_datatype_struct(dst_type);
    CHECK_EQUAL_BASETYPE(dts_src, dts_dst);

    *handleptr = DART_HANDLE_NULL;
    dart_handle_t handle = *handleptr;

    // initialized with relative team unit id
    dart_unit_t global_dst_unit_id = gptr.unitid;

    gaspi_segment_id_t gaspi_dst_seg_id = 0;
    DART_CHECK_ERROR(glob_unit_gaspi_seg(&gptr, &global_dst_unit_id, &gaspi_dst_seg_id, "dart_put_handle"));

    dart_global_unit_t global_myid;
    DART_CHECK_ERROR(dart_myid(&global_myid));

    converted_type_t conv_type;
    DART_CHECK_ERROR(dart_convert_type(dts_src, dts_dst, nelem, &conv_type));

    if(global_myid.id == global_dst_unit_id)
    {
        DART_LOG_TRACE("dart_put_handle: local put -> empty handle");
        DART_CHECK_GASPI_ERROR_CLEAN(conv_type,
            local_put(&gptr, gaspi_dst_seg_id, src, &conv_type));
    }
    else
    {
        // get gaspi segment id and bind it to dst
        gaspi_segment_id_t free_seg_id;
        DART_CHECK_GASPI_ERROR_CLEAN(conv_type,
            seg_stack_pop(&pool_gaspi_seg_ids, &free_seg_id));

        gaspi_queue_id_t queue = (gaspi_queue_id_t) -1;

        DART_CHECK_GASPI_ERROR_CLEAN_SEG(free_seg_id, conv_type,
            remote_put(&gptr,
                      global_dst_unit_id,
                      gaspi_dst_seg_id,
                      free_seg_id,
                      src,
                      &queue,
                      &conv_type)
        );

        DART_CHECK_GASPI_ERROR_CLEAN_SEG(free_seg_id, conv_type,
            gaspi_notify(free_seg_id, global_myid.id, free_seg_id, free_seg_id, queue, GASPI_BLOCK));

        DART_CHECK_GASPI_ERROR_CLEAN_SEG(free_seg_id, conv_type,
            put_completion_test(global_dst_unit_id, queue));

        DART_CHECK_GASPI_ERROR_CLEAN_SEG(free_seg_id, conv_type,
            gaspi_notify(free_seg_id, global_myid.id, put_completion_dst_seg, put_completion_dst_seg, queue, GASPI_BLOCK));

        handle = (dart_handle_t) malloc(sizeof(struct dart_handle_struct));
        handle->comm_kind     = GASPI_WRITE;
        handle->queue         = queue;
        handle->local_seg_id  = free_seg_id;
        handle->notify_remote = (gaspi_notification_id_t) put_completion_dst_seg;
        *handleptr = handle;
        DART_LOG_DEBUG("dart_put_handle: finished remote put and constructed handle[GASPI_READ, %d, %d, %d]", queue, free_seg_id, put_completion_dst_seg);
    }

    free_converted_type(&conv_type);

    DART_LOG_DEBUG("dart_put_handle: handle(%p) dest:%d\n", (void*)(handle), global_dst_unit_id);

    return DART_OK;
}

dart_ret_t dart_flush(dart_gptr_t gptr)
{
    request_table_entry_t* request_entry = NULL;
    DART_CHECK_ERROR(find_rma_request(gptr.unitid, gptr.segid, &request_entry));
    if(request_entry == NULL)
    {
        DART_LOG_DEBUG("dart_flush: no queue found");

        return DART_OK;
    }

    DART_CHECK_GASPI_ERROR(gaspi_wait(request_entry->queue, GASPI_BLOCK));

    DART_CHECK_ERROR(free_segment_ids(request_entry));

    return DART_OK;
}

dart_ret_t dart_flush_all(dart_gptr_t gptr)
{
    request_table_entry_t* request_entry = NULL;

    request_iterator_t iter = new_request_iter(gptr.segid);
    if(request_iter_is_vaild(iter)){
      while(request_iter_is_vaild(iter))
      {
          DART_CHECK_ERROR(request_iter_get_entry(iter, &request_entry));

          DART_CHECK_ERROR(free_segment_ids(request_entry));

          DART_CHECK_ERROR(request_iter_next(iter));
      }

      DART_CHECK_ERROR(destroy_request_iter(iter));
   }
    return DART_OK;
}

dart_ret_t dart_flush_local(dart_gptr_t gptr)
{
    DART_CHECK_ERROR(dart_flush(gptr));

    return DART_OK;
}

dart_ret_t dart_flush_local_all(dart_gptr_t gptr)
{
    DART_CHECK_ERROR(dart_flush_all(gptr));

    return DART_OK;
}

dart_ret_t dart_get(
  void            * dst,
  dart_gptr_t       gptr,
  size_t            nelem,
  dart_datatype_t   src_type,
  dart_datatype_t   dst_type)
{
    dart_datatype_struct_t* dts_src = get_datatype_struct(src_type);
    dart_datatype_struct_t* dts_dst = get_datatype_struct(dst_type);
    CHECK_EQUAL_BASETYPE(dts_src, dts_dst);

    size_t nbytes_elem = datatype_sizeof(datatype_base_struct(dts_src));
    size_t nbytes_segment = nbytes_elem * nelem;

    // initialized with relative team unit id
    dart_unit_t global_src_unit_id = gptr.unitid;

    gaspi_segment_id_t gaspi_src_seg_id = 0;
    DART_CHECK_ERROR(glob_unit_gaspi_seg(&gptr, &global_src_unit_id, &gaspi_src_seg_id, "dart_get_handled"));

    dart_global_unit_t global_myid;
    DART_CHECK_ERROR(dart_myid(&global_myid));

    converted_type_t conv_type;
    DART_CHECK_ERROR(dart_convert_type(dts_src, dts_dst, nelem, &conv_type));

    if(global_myid.id == global_src_unit_id)
    {
        DART_CHECK_GASPI_ERROR_CLEAN(conv_type,
            local_get(&gptr, gaspi_src_seg_id, dst, &conv_type));
    }
    else
    {
        // get gaspi segment id and bind it to dst
        gaspi_segment_id_t free_seg_id;
        DART_CHECK_GASPI_ERROR_CLEAN(conv_type,
            seg_stack_pop(&pool_gaspi_seg_ids, &free_seg_id));

        // communitcation request
        request_table_entry_t* request_entry = NULL;
        DART_CHECK_GASPI_ERROR_CLEAN(conv_type,
            add_rma_request_entry(gptr.unitid, gptr.segid, free_seg_id, &request_entry));
        DART_LOG_TRACE("Start remote get src_seg: %d dst_seg: %d", gaspi_src_seg_id, free_seg_id);
        DART_CHECK_GASPI_ERROR_CLEAN_SEG(free_seg_id, conv_type,
            remote_get(&gptr,
                      global_src_unit_id,
                      gaspi_src_seg_id,
                      free_seg_id,
                      dst,
                      &(request_entry->queue),
                      &conv_type)
        );
    }

    free_converted_type(&conv_type);

   return DART_OK;
}

dart_ret_t dart_put(
  dart_gptr_t       gptr,
  const void      * src,
  size_t            nelem,
  dart_datatype_t   src_type,
  dart_datatype_t   dst_type)
{
    dart_datatype_struct_t* dts_src = get_datatype_struct(src_type);
    dart_datatype_struct_t* dts_dst = get_datatype_struct(dst_type);
    CHECK_EQUAL_BASETYPE(dts_src, dts_dst);

    // initialized with relative team unit id
    dart_unit_t global_dst_unit_id = gptr.unitid;

    gaspi_segment_id_t gaspi_dst_seg_id = 0;
    DART_CHECK_ERROR(glob_unit_gaspi_seg(&gptr, &global_dst_unit_id, &gaspi_dst_seg_id, "dart_put_handle"));

    dart_global_unit_t global_myid;
    DART_CHECK_ERROR(dart_myid(&global_myid));

    converted_type_t conv_type;
    DART_CHECK_ERROR(dart_convert_type(dts_src, dts_dst, nelem, &conv_type));

    if(global_myid.id == global_dst_unit_id)
    {
        DART_CHECK_GASPI_ERROR_CLEAN(conv_type,
            local_put(&gptr, gaspi_dst_seg_id, src, &conv_type));
    }
    else
    {
        // get gaspi segment id and bind it to dst
        gaspi_segment_id_t free_seg_id;
        DART_CHECK_GASPI_ERROR_CLEAN(conv_type,
            seg_stack_pop(&pool_gaspi_seg_ids, &free_seg_id));

        // communitcation request
        request_table_entry_t* request_entry = NULL;
        DART_CHECK_GASPI_ERROR_CLEAN(conv_type,
                    add_rma_request_entry(gptr.unitid, gptr.segid, free_seg_id, &request_entry));
        DART_LOG_TRACE("Start remote put");
        DART_CHECK_GASPI_ERROR_CLEAN_SEG(free_seg_id, conv_type,
            remote_put(&gptr,
                      global_dst_unit_id,
                      gaspi_dst_seg_id,
                      free_seg_id,
                      src,
                      &(request_entry->queue),
                      &conv_type)
        );

        DART_CHECK_GASPI_ERROR_CLEAN_SEG(free_seg_id, conv_type,
            put_completion_test(global_dst_unit_id, request_entry->queue));
    }

    free_converted_type(&conv_type);

   return DART_OK;
}

#define DART_GET_OP_INT(_op_name) \
    case DART_TYPE_SHORT: return DART_NAME_OP(_op_name, short); \
    case DART_TYPE_INT: return DART_NAME_OP(_op_name, int);     \
    case DART_TYPE_UINT: return DART_NAME_OP(_op_name, uInt); \
    case DART_TYPE_LONG: return DART_NAME_OP(_op_name, long); \
    case DART_TYPE_ULONG: return DART_NAME_OP(_op_name, uLong); \
    case DART_TYPE_LONGLONG: return DART_NAME_OP(_op_name, longLong); \
    case DART_TYPE_ULONGLONG: return DART_NAME_OP(_op_name, uLongLong);

#define DART_GET_OP_INT_BYTE(_op_name) \
   case DART_TYPE_BYTE: return DART_NAME_OP(_op_name, char); \
   DART_GET_OP_INT(_op_name)

#define DART_GET_OP_ALL(_op_name) \
   DART_GET_OP_INT_BYTE(_op_name) \
   case DART_TYPE_FLOAT: return DART_NAME_OP(_op_name, float); \
   case DART_TYPE_DOUBLE: return DART_NAME_OP(_op_name, double); \
   case DART_TYPE_LONG_DOUBLE: return DART_NAME_OP(_op_name, longDouble); \

#define DART_GET_OP(_op_name, _types) \
    switch(dtype) \
    { \
        DART_GET_OP_##_types(_op_name) \
        default: return NULL; \
    }

gaspi_reduce_operation_t gaspi_get_op(dart_operation_t op, dart_datatype_t dtype)
{
    switch(op)
    {
      case DART_OP_MINMAX: DART_GET_OP(MINMAX, ALL)
      case DART_OP_MIN   : DART_GET_OP(MIN, ALL)
      case DART_OP_MAX   : DART_GET_OP(MAX, ALL)
      case DART_OP_SUM   : DART_GET_OP(SUM, ALL)
      case DART_OP_PROD  : DART_GET_OP(PROD, ALL)
      case DART_OP_LAND  : DART_GET_OP(LAND, INT)
      case DART_OP_LOR   : DART_GET_OP(LOR, INT)
      case DART_OP_LXOR  : DART_GET_OP(LXOR, INT)
      case DART_OP_BAND  : DART_GET_OP(BAND, INT_BYTE)
      case DART_OP_BOR   : DART_GET_OP(BOR, INT_BYTE)
      case DART_OP_BXOR  : DART_GET_OP(BXOR, INT_BYTE)
      default: DART_LOG_ERROR("ERROR: Operation not supported!");
    }

    return NULL;
}
dart_ret_t dart_allreduce(
  const void       * sendbuf,
  void             * recvbuf,
  size_t             nelem,
  dart_datatype_t    dtype,
  dart_operation_t   op,
  dart_team_t        team)
{
    dart_datatype_struct_t* dts = get_datatype_struct(dtype);
    if(!datatype_isbasic(dts))
    {
      DART_LOG_ERROR("dart_allreduce: Complex datatypes are not supported!");
      return DART_ERR_INVAL;
    }

    uint16_t index;
    if(dart_adapt_teamlist_convert(team, &index) == -1)
    {
      DART_LOG_ERROR("dart_allreduce: Can't find index of given team!");
      return DART_ERR_OTHER;
    }

    gaspi_reduce_operation_t gaspi_op = gaspi_get_op(op, dtype);
    if(gaspi_op == NULL)
    {
      DART_LOG_ERROR("dart_allreduce: Operation not supported!");
      return DART_ERR_INVAL;
    }

    DART_LOG_DEBUG("User allreduce from (%p) to (%p) for %d elements", (void* const)sendbuf, recvbuf, nelem);
    gaspi_allreduce_user((void* const)sendbuf, recvbuf, nelem,
                        datatype_sizeof(dts), gaspi_op,
                        NULL, dart_teams[index].id, GASPI_BLOCK);

    return DART_OK;
}

/* slow and simple version of reduce
 * TODO: more efficient version e.g. inverted binary tree
 */
dart_ret_t dart_reduce(
  const void       * sendbuf,
  void             * recvbuf,
  size_t             nelem,
  dart_datatype_t    dtype,
  dart_operation_t   op,
  dart_team_unit_t   root,
  dart_team_t        team)
{
    dart_datatype_struct_t* dts = get_datatype_struct(dtype);
    if(!datatype_isbasic(dts))
    {
      DART_LOG_ERROR("dart_reduce: complex datatypes are not supported!");

      return DART_ERR_INVAL;
    }

    size_t team_size;
    DART_CHECK_ERROR(dart_team_size(team, &team_size));

    gaspi_reduce_operation_t gaspi_op = gaspi_get_op(op, dtype);
    if(gaspi_op == NULL)
    {
      DART_LOG_ERROR("dart_reduce: operation not supported!");
      return DART_ERR_INVAL;
    }

    //TODO error handling
    size_t nbytes_elem = datatype_sizeof(dts);
    void* recv_tmp = malloc(nbytes_elem * team_size);
    DART_LOG_DEBUG("dart_reduce: gather in team: %d on root: %d", team, root);
    DART_CHECK_ERROR(dart_gather(sendbuf, recv_tmp, nelem, dtype, root, team));

    void* buffer_tmp = recv_tmp;
    DART_LOG_DEBUG("dart_reduce: starting op with op1(%p), op2(%p) advance op2 by nbytes_elem: %d for teamsize: %d times", recvbuf, buffer_tmp, nbytes_elem, team_size);
    for(int i = 0; i < team_size; ++i, buffer_tmp += nbytes_elem){
         DART_CHECK_GASPI_ERROR(
            gaspi_op(recvbuf, buffer_tmp, recvbuf, NULL, nelem, nbytes_elem, GASPI_BLOCK));
      }

    free(recv_tmp);

    dart_barrier(team);

  return DART_OK;
}

/* Warning: This implementation ignores tags */
dart_ret_t dart_recv(
  void                * recvbuf,
  size_t                nelem,
  dart_datatype_t       dtype,
  int                   tag,
  dart_global_unit_t    unit)
{
    dart_datatype_struct_t* dts = get_datatype_struct(dtype);
    if(!datatype_isbasic(dts))
    {
      DART_LOG_ERROR("dart_recv: complex datatypes are not supported!");
      return DART_ERR_INVAL;
    }

    size_t nbytes_elem = datatype_sizeof(dts);

    // get gaspi segment id and bind it to dst
    gaspi_segment_id_t free_seg_id;
    DART_CHECK_GASPI_ERROR(seg_stack_pop(&pool_gaspi_seg_ids, &free_seg_id));
    DART_LOG_TRACE("dart_recv: got free_seg_id: %d to bind", free_seg_id);

    DART_CHECK_GASPI_ERROR(gaspi_segment_bind(free_seg_id, recvbuf, nbytes_elem, 0));
    gaspi_rank_t rank;
    gaspi_passive_receive(free_seg_id, 0, &rank, nbytes_elem, GASPI_BLOCK);

    DART_LOG_TRACE("dart_recv: returning segment: %d to stack after receive", free_seg_id);
    DART_CHECK_GASPI_ERROR(gaspi_segment_delete(free_seg_id));
    DART_CHECK_ERROR(seg_stack_push(&pool_gaspi_seg_ids, free_seg_id));

    if(rank != unit.id)
    {
      DART_LOG_ERROR("dart_recv: rank id of sender doesn't match.");

      return DART_ERR_OTHER;
    }

    return DART_OK;
}

/* Warning: This implementation ignores tags */
dart_ret_t dart_send(
  const void         * sendbuf,
  size_t               nelem,
  dart_datatype_t      dtype,
  int                  tag,
  dart_global_unit_t   unit)
{
    dart_datatype_struct_t* dts = get_datatype_struct(dtype);
    if(!datatype_isbasic(dts))
    {
      DART_LOG_ERROR("dart_send: complex datatypes are not supported!");

      return DART_ERR_INVAL;
    }

    size_t nbytes_elem = datatype_sizeof(dts);

    // get gaspi segment id and bind it to dst
    gaspi_segment_id_t free_seg_id;
    DART_CHECK_GASPI_ERROR(seg_stack_pop(&pool_gaspi_seg_ids, &free_seg_id));
    DART_CHECK_GASPI_ERROR(gaspi_segment_bind(free_seg_id, (void* const) sendbuf, nbytes_elem, 0));
    DART_LOG_TRACE("dart_send: got free_seg_id: %d to bind to", free_seg_id);

    gaspi_passive_send(free_seg_id, 0, unit.id, nbytes_elem, GASPI_BLOCK);

    DART_LOG_TRACE("dart_send: returning free_seg_id: %d to stack after send", free_seg_id);
    DART_CHECK_GASPI_ERROR(gaspi_segment_delete(free_seg_id));
    DART_CHECK_ERROR(seg_stack_push(&pool_gaspi_seg_ids, free_seg_id));

    return DART_OK;
}

dart_ret_t dart_sendrecv(
  const void         * sendbuf,
  size_t               send_nelem,
  dart_datatype_t      send_dtype,
  int                  send_tag,
  dart_global_unit_t   dest,
  void               * recvbuf,
  size_t               recv_nelem,
  dart_datatype_t      recv_dtype,
  int                  recv_tag,
  dart_global_unit_t   src)
{
    DART_LOG_ERROR("dart_sendrecv: dart_sendrecv not supported!");

    return DART_ERR_INVAL;
}

//Needs to be implemented
dart_ret_t dart_fetch_and_op(
  dart_gptr_t      gptr,
  const void *     value,
  void *           result,
  dart_datatype_t  dtype,
  dart_operation_t op)
{
  if(value == NULL || result == NULL)
  {
      DART_LOG_ERROR("dart_fetch_and_op: No valid adress (NULL)");
      return DART_ERR_INVAL;
  }

  if(op != DART_OP_SUM)
  {
      DART_LOG_ERROR("dart_fetch_and_op: dart_fetch_and_op operator not supported.");
      return DART_ERR_INVAL;
  }

  dart_unit_t global_dst_unit_id;
  gaspi_segment_id_t gaspi_dst_seg_id;
  DART_CHECK_ERROR(glob_unit_gaspi_seg(&gptr, &global_dst_unit_id, &gaspi_dst_seg_id, "dart_put_handle"));

  gaspi_atomic_value_t* value_old = (gaspi_atomic_value_t*) result;
  DART_CHECK_GASPI_ERROR(
        gaspi_atomic_fetch_add(global_dst_unit_id,
                               gptr.addr_or_offs.offset,
                               global_dst_unit_id,
                               *((gaspi_atomic_value_t*) value),
                               value_old,
                               GASPI_BLOCK));

  return DART_OK;
}

//Needs to be implemented
dart_ret_t dart_accumulate(
  dart_gptr_t      gptr,
  const void *     value,
  size_t           nelem,
  dart_datatype_t  dtype,
  dart_operation_t op)
{
    DART_LOG_ERROR("dart_accumulate_blocking_local for gaspi not supported!");
    return DART_ERR_INVAL;
}

dart_ret_t dart_accumulate_blocking_local(
    dart_gptr_t      gptr,
    const void     * values,
    size_t           nelem,
    dart_datatype_t  dtype,
    dart_operation_t op)
{
    DART_LOG_ERROR("dart_accumulate_blocking_local for gaspi not supported!");
    return DART_ERR_INVAL;
}

dart_ret_t dart_compare_and_swap(
    dart_gptr_t      gptr,
    const void     * value,
    const void     * compare,
    void           * result,
    dart_datatype_t  dtype)
{
  if(value == NULL || compare == NULL || result == NULL)
  {
      DART_LOG_ERROR("dart_compare_and_swap: No valid adress (NULL)");
      return DART_ERR_INVAL;
  }

  dart_unit_t global_dst_unit_id;
  gaspi_segment_id_t gaspi_dst_seg_id;
  DART_CHECK_ERROR(glob_unit_gaspi_seg(&gptr, &global_dst_unit_id, &gaspi_dst_seg_id, "dart_put_handle"));

  gaspi_atomic_value_t* value_old = (gaspi_atomic_value_t*) result;
  DART_CHECK_GASPI_ERROR(
        gaspi_atomic_compare_swap(global_dst_unit_id,
                               gptr.addr_or_offs.offset,
                               global_dst_unit_id,
                               *((gaspi_atomic_value_t*) compare),
                               *((gaspi_atomic_value_t*) value),
                               value_old,
                               GASPI_BLOCK));

  return DART_OK;
}
