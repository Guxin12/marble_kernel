// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2017-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022-2025 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/debugfs.h>
#include <linux/videodev2.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <media/cam_sync.h>
#include <media/cam_defs.h>

#include "cam_context.h"
#include "cam_context_utils.h"
#include "cam_mem_mgr.h"
#include "cam_node.h"
#include "cam_req_mgr_util.h"
#include "cam_sync_api.h"
#include "cam_trace.h"
#include "cam_debug_util.h"
#include "cam_cpas_api.h"
#include "cam_packet_util.h"
#include "cam_common_util.h"

static uint cam_debug_ctx_req_list;
module_param(cam_debug_ctx_req_list, uint, 0644);

static inline int cam_context_validate_thread(void)
{
	if (in_interrupt()) {
		WARN(1, "Invalid execution context\n");
		return -EINVAL;
	}
	return 0;
}

static void cam_context_free_mem_hw_entries(struct cam_context *ctx)
{
	int  i;

	if (ctx->out_map_entries) {
		for (i = 0; i < ctx->req_size; i++) {
			kfree(ctx->out_map_entries[i]);
			ctx->out_map_entries[i] = NULL;
		}

		kfree(ctx->out_map_entries);
		ctx->out_map_entries = NULL;
	}

	if (ctx->in_map_entries) {
		for (i = 0; i < ctx->req_size; i++) {
			kfree(ctx->in_map_entries[i]);
			ctx->in_map_entries[i] = NULL;
		}

		kfree(ctx->in_map_entries);
		ctx->in_map_entries = NULL;
	}

	if (ctx->hw_update_entry) {
		for (i = 0; i < ctx->req_size; i++) {
			kfree(ctx->hw_update_entry[i]);
			ctx->hw_update_entry[i] = NULL;
		}

		kfree(ctx->hw_update_entry);
		ctx->hw_update_entry = NULL;
	}
}

static int cam_context_allocate_mem_hw_entries(struct cam_context *ctx)
{
	int rc = 0;
	unsigned int i;
	struct cam_ctx_request          *req;
	struct cam_ctx_request          *temp_req;

	CAM_DBG(CAM_CTXT,
		"%s[%d] num: max_hw %u in_map %u out_map %u req %u",
		ctx->dev_name,
		ctx->ctx_id,
		ctx->max_hw_update_entries,
		ctx->max_in_map_entries,
		ctx->max_out_map_entries,
		ctx->req_size);

	ctx->hw_update_entry = kcalloc(ctx->req_size,
		sizeof(struct cam_hw_update_entry *), GFP_KERNEL);

	if (!ctx->hw_update_entry) {
		CAM_ERR(CAM_CTXT, "%s[%d] no memory for hw_update_entry",
			ctx->dev_name, ctx->ctx_id);
		return -ENOMEM;
	}

	for (i = 0; i < ctx->req_size; i++) {
		ctx->hw_update_entry[i] = kcalloc(ctx->max_hw_update_entries,
			sizeof(struct cam_hw_update_entry), GFP_KERNEL);

		if (!ctx->hw_update_entry[i]) {
			CAM_ERR(CAM_CTXT, "%s[%d] no memory for hw_update_entry: %u",
				ctx->dev_name, ctx->ctx_id, i);
			rc = -ENOMEM;
			goto free_mem;
		}
	}

	ctx->in_map_entries = kcalloc(ctx->req_size, sizeof(struct cam_hw_fence_map_entry *),
		GFP_KERNEL);

	if (!ctx->in_map_entries) {
		CAM_ERR(CAM_CTXT, "%s[%d] no memory for in_map_entries",
			ctx->dev_name, ctx->ctx_id);
		rc = -ENOMEM;
		goto free_mem;
	}

	for (i = 0; i < ctx->req_size; i++) {
		ctx->in_map_entries[i] = kcalloc(ctx->max_in_map_entries,
			sizeof(struct cam_hw_fence_map_entry), GFP_KERNEL);

		if (!ctx->in_map_entries[i]) {
			CAM_ERR(CAM_CTXT, "%s[%d] no memory for in_map_entries: %u",
				ctx->dev_name, ctx->ctx_id, i);
			rc = -ENOMEM;
			goto free_mem;
		}
	}

	ctx->out_map_entries = kcalloc(ctx->req_size, sizeof(struct cam_hw_fence_map_entry *),
		GFP_KERNEL);

	if (!ctx->out_map_entries) {
		CAM_ERR(CAM_CTXT, "%s[%d] no memory for out_map_entries",
			ctx->dev_name, ctx->ctx_id);
		rc = -ENOMEM;
		goto free_mem;
	}

	for (i = 0; i < ctx->req_size; i++) {
		ctx->out_map_entries[i] = kcalloc(ctx->max_out_map_entries,
			sizeof(struct cam_hw_fence_map_entry), GFP_KERNEL);

		if (!ctx->out_map_entries[i]) {
			CAM_ERR(CAM_CTXT, "%s[%d] no memory for out_map_entries: %u",
				ctx->dev_name, ctx->ctx_id, i);
			rc = -ENOMEM;
			goto free_mem;
		}
	}

	list_for_each_entry_safe(req, temp_req, &ctx->free_req_list, list) {
		req->hw_update_entries = ctx->hw_update_entry[req->index];
		req->in_map_entries = ctx->in_map_entries[req->index];
		req->out_map_entries = ctx->out_map_entries[req->index];
	}

	return rc;

free_mem:
	cam_context_free_mem_hw_entries(ctx);
	return rc;
}

int cam_context_buf_done_from_hw(struct cam_context *ctx,
	void *done_event_data, uint32_t evt_id)
{
	int j;
	int result;
	struct cam_ctx_request *req;
	struct cam_hw_done_event_data *done =
		(struct cam_hw_done_event_data *)done_event_data;
	int rc;

	if (!ctx || !done) {
		CAM_ERR(CAM_CTXT, "Invalid input params %pK %pK", ctx, done);
		return -EINVAL;
	}

	rc = cam_context_validate_thread();
	if (rc)
		return rc;

	spin_lock(&ctx->lock);
	if (list_empty(&ctx->active_req_list)) {
		CAM_ERR(CAM_CTXT, "[%s][%d] no active request",
			ctx->dev_name, ctx->ctx_id);
		spin_unlock(&ctx->lock);
		return -EIO;
	}
	req = list_first_entry(&ctx->active_req_list,
		struct cam_ctx_request, list);

	trace_cam_buf_done("UTILS", ctx, req);

	if (done->request_id != req->request_id) {
		CAM_ERR(CAM_CTXT,
			"[%s][%d] mismatch: done req[%lld], active req[%lld]",
			ctx->dev_name, ctx->ctx_id,
			done->request_id, req->request_id);
		spin_unlock(&ctx->lock);
		return -EIO;
	}

	if (!req->num_out_map_entries) {
		CAM_ERR(CAM_CTXT, "[%s][%d] no output fence to signal",
			ctx->dev_name, ctx->ctx_id);
		spin_unlock(&ctx->lock);
		return -EIO;
	}

	/*
	 * since another thread may be adding/removing from active
	 * list, so hold the lock
	 */
	list_del_init(&req->list);
	spin_unlock(&ctx->lock);
	if (evt_id == CAM_CTX_EVT_ID_SUCCESS)
		result = CAM_SYNC_STATE_SIGNALED_SUCCESS;
	else  if (evt_id == CAM_CTX_EVT_ID_CANCEL)
		result = CAM_SYNC_STATE_SIGNALED_CANCEL;
	else
		result = CAM_SYNC_STATE_SIGNALED_ERROR;

	CAM_DBG(CAM_REQ,
		"[%s][ctx_id %d] : req[%llu] : Signaling %d",
		ctx->dev_name, ctx->ctx_id, req->request_id, result);

	for (j = 0; j < req->num_out_map_entries; j++) {
		/* Get buf handles from packet and retrieve them from presil framework */
		if (cam_presil_mode_enabled()) {
			rc = cam_presil_retrieve_buffers_from_packet(req->pf_data.packet,
				ctx->img_iommu_hdl, req->out_map_entries[j].resource_handle);
			if (rc) {
				CAM_ERR(CAM_CTXT, "Failed to retrieve image buffers rc:%d", rc);
				return rc;
			}
		}

		CAM_DBG(CAM_REQ, "fence %d signal with %d",
			req->out_map_entries[j].sync_id, result);
		cam_sync_signal(req->out_map_entries[j].sync_id, result,
			done->evt_param);
		req->out_map_entries[j].sync_id = -1;
	}

	if (cam_debug_ctx_req_list & ctx->dev_id)
		CAM_INFO(CAM_CTXT,
			"[%s][%d] : Moving req[%llu] from active_list to free_list",
			ctx->dev_name, ctx->ctx_id, req->request_id);

	cam_cpas_notify_event(ctx->ctx_id_string, req->request_id);

	/*
	 * another thread may be adding/removing from free list,
	 * so hold the lock
	 */
	if (req->packet) {
		cam_common_mem_free(req->packet);
		req->packet = NULL;
	}
	spin_lock(&ctx->lock);
	list_add_tail(&req->list, &ctx->free_req_list);
	req->ctx = NULL;
	spin_unlock(&ctx->lock);

	return 0;
}

static int cam_context_apply_req_to_hw(struct cam_ctx_request *req,
	struct cam_req_mgr_apply_request *apply)
{
	int rc = 0;
	struct cam_context *ctx = req->ctx;
	struct cam_hw_config_args cfg;

	if (!ctx->hw_mgr_intf) {
		CAM_ERR(CAM_CTXT, "[%s][%d] HW interface is not ready",
			ctx->dev_name, ctx->ctx_id);
		rc = -EFAULT;
		goto end;
	}

	spin_lock(&ctx->lock);
	list_del_init(&req->list);
	list_add_tail(&req->list, &ctx->active_req_list);
	spin_unlock(&ctx->lock);

	if (cam_debug_ctx_req_list & ctx->dev_id)
		CAM_INFO(CAM_CTXT,
			"[%s][%d] : Moving req[%llu] from pending_list to active_list",
			ctx->dev_name, ctx->ctx_id, req->request_id);

	cfg.ctxt_to_hw_map = ctx->ctxt_to_hw_map;
	cfg.request_id = req->request_id;
	cfg.hw_update_entries = req->hw_update_entries;
	cfg.num_hw_update_entries = req->num_hw_update_entries;
	cfg.out_map_entries = req->out_map_entries;
	cfg.num_out_map_entries = req->num_out_map_entries;
	cfg.priv = req->req_priv;

	rc = ctx->hw_mgr_intf->hw_config(ctx->hw_mgr_intf->hw_mgr_priv, &cfg);
	if (rc) {
		if (req->packet) {
			cam_common_mem_free(req->packet);
			req->packet = NULL;
		}
		spin_lock(&ctx->lock);
		list_del_init(&req->list);
		list_add_tail(&req->list, &ctx->free_req_list);
		spin_unlock(&ctx->lock);

		if (cam_debug_ctx_req_list & ctx->dev_id)
			CAM_INFO(CAM_CTXT,
				"[%s][%d] : Moving req[%llu] from active_list to free_list",
				ctx->dev_name, ctx->ctx_id, req->request_id);
	}

end:
	return rc;
}

static void cam_context_sync_callback(int32_t sync_obj, int status, void *data)
{
	struct cam_ctx_request *req = data;
	struct cam_context *ctx = NULL;
	struct cam_flush_dev_cmd flush_cmd;
	struct cam_req_mgr_apply_request apply;
	int rc;

	if (!req) {
		CAM_ERR(CAM_CTXT, "Invalid input param");
		return;
	}
	rc = cam_context_validate_thread();
	if (rc)
		return;

	ctx = req->ctx;
	if (!ctx) {
		CAM_ERR(CAM_CTXT, "Invalid ctx for req %llu", req->request_id);
		return;
	}

	if (atomic_inc_return(&req->num_in_acked) == req->num_in_map_entries) {
		apply.request_id = req->request_id;
		/*
		 * take mutex to ensure that another thread does
		 * not flush the request while this
		 * thread is submitting it to h/w. The submit to
		 * h/w and adding to the active list should happen
		 * in a critical section which is provided by this
		 * mutex.
		 */
		if ((status == CAM_SYNC_STATE_SIGNALED_ERROR) ||
			(status == CAM_SYNC_STATE_SIGNALED_CANCEL)) {
			CAM_DBG(CAM_CTXT, "fence error: %d on obj %d",
				status, sync_obj);
			flush_cmd.req_id = req->request_id;
			cam_context_flush_req_to_hw(ctx, &flush_cmd);
		}

		mutex_lock(&ctx->sync_mutex);
		if (!req->flushed) {
			cam_context_apply_req_to_hw(req, &apply);
			mutex_unlock(&ctx->sync_mutex);
		} else {
			req->flushed = 0;
			req->ctx = NULL;
			mutex_unlock(&ctx->sync_mutex);
			spin_lock(&ctx->lock);
			list_del_init(&req->list);
			if (req->packet) {
				cam_common_mem_free(req->packet);
				req->packet = NULL;
			}
			list_add_tail(&req->list, &ctx->free_req_list);
			spin_unlock(&ctx->lock);

			if (cam_debug_ctx_req_list & ctx->dev_id)
				CAM_INFO(CAM_CTXT,
					"[%s][%d] : Moving req[%llu] from pending_list to free_list",
					ctx->dev_name, ctx->ctx_id,
					req->request_id);
		}
	}
	cam_context_putref(ctx);
}

int32_t cam_context_release_dev_to_hw(struct cam_context *ctx,
	struct cam_release_dev_cmd *cmd)
{
	struct cam_hw_release_args arg;

	if (!ctx) {
		CAM_ERR(CAM_CTXT, "Invalid input param");
		return -EINVAL;
	}

	if ((!ctx->hw_mgr_intf) || (!ctx->hw_mgr_intf->hw_release)) {
		CAM_ERR(CAM_CTXT, "[%s][%d] HW interface is not ready",
			ctx->dev_name, ctx->ctx_id);
		return -EINVAL;
	}

	arg.ctxt_to_hw_map = ctx->ctxt_to_hw_map;
	arg.active_req = false;

	ctx->hw_mgr_intf->hw_release(ctx->hw_mgr_intf->hw_mgr_priv, &arg);
	cam_context_free_mem_hw_entries(ctx);
	ctx->ctxt_to_hw_map = NULL;

	ctx->session_hdl = -1;
	ctx->dev_hdl = -1;
	ctx->link_hdl = -1;

	return 0;
}

int32_t cam_context_config_dev_to_hw(
	struct cam_context *ctx, struct cam_config_dev_cmd *cmd)
{
	int rc = 0;
	size_t len;
	struct cam_hw_stream_setttings cfg;
	uintptr_t packet_addr;
	struct cam_packet *packet_u;
	struct cam_packet *packet = NULL;
	size_t remain_len = 0;

	if (!ctx || !cmd) {
		CAM_ERR(CAM_CTXT, "Invalid input params %pK %pK", ctx, cmd);
		return -EINVAL;
	}

	if (!ctx->hw_mgr_intf->hw_config_stream_settings) {
		CAM_ERR(CAM_CTXT, "[%s][%d] HW interface is not ready",
			ctx->dev_name, ctx->ctx_id);
		rc = -EFAULT;
		return rc;
	}

	rc = cam_context_validate_thread();
	if (rc) {
		CAM_ERR(CAM_CTXT,
			"Not executing in the right context");
		return rc;
	}

	rc = cam_mem_get_cpu_buf((int32_t) cmd->packet_handle,
		&packet_addr, &len);
	if (rc) {
		CAM_ERR(CAM_CTXT, "[%s][%d] Can not get packet address",
			ctx->dev_name, ctx->ctx_id);
		rc = -EINVAL;
		return rc;
	}

	packet_u = (struct cam_packet *) ((uint8_t *)packet_addr +
		(uint32_t)cmd->offset);
	remain_len = len - (uint32_t)cmd->offset;

	rc = cam_packet_util_copy_pkt_to_kmd(packet_u, &packet, remain_len);
	if (rc) {
		CAM_ERR(CAM_CTXT, "Copying packet to KMD failed");
		goto put_ref;
	}

	memset(&cfg, 0, sizeof(cfg));
	cfg.packet = packet;
	cfg.ctxt_to_hw_map = ctx->ctxt_to_hw_map;
	cfg.priv = NULL;

	CAM_DBG(CAM_CTXT, "Processing config settings");
	rc = ctx->hw_mgr_intf->hw_config_stream_settings(
		ctx->hw_mgr_intf->hw_mgr_priv, &cfg);
	if (rc) {
		CAM_ERR(CAM_CTXT,
			"[%s][%d] Config failed stream settings",
			ctx->dev_name, ctx->ctx_id);
		rc = -EFAULT;
	}

	cam_common_mem_free(packet);
	packet = NULL;
put_ref:
	cam_mem_put_cpu_buf((int32_t) cmd->packet_handle);
	return rc;
}

int32_t cam_context_prepare_dev_to_hw(struct cam_context *ctx,
	struct cam_config_dev_cmd *cmd)
{
	int rc = 0;
	struct cam_ctx_request *req = NULL;
	struct cam_hw_prepare_update_args cfg;
	struct cam_packet *packet = NULL;
	size_t remain_len = 0;
	int32_t i = 0, j = 0;

	if (!ctx || !cmd) {
		CAM_ERR(CAM_CTXT, "Invalid input params %pK %pK", ctx, cmd);
		return -EINVAL;
	}

	if (!ctx->hw_mgr_intf) {
		CAM_ERR(CAM_CTXT, "[%s][%d] HW interface is not ready",
			ctx->dev_name, ctx->ctx_id);
		return -EFAULT;
	}
	rc = cam_context_validate_thread();
	if (rc)
		return rc;

	spin_lock(&ctx->lock);
	if (!list_empty(&ctx->free_req_list)) {
		req = list_first_entry(&ctx->free_req_list,
			struct cam_ctx_request, list);
		list_del_init(&req->list);
	}
	spin_unlock(&ctx->lock);

	if (!req) {
		CAM_ERR(CAM_CTXT, "[%s][%d] No more request obj free",
			ctx->dev_name, ctx->ctx_id);
		return -ENOMEM;
	}

	if (req->packet) {
		CAM_WARN(CAM_CTXT, "[%s][%d] Missing free request local packet",
			ctx->dev_name, ctx->ctx_id);
		cam_common_mem_free(req->packet);
		req->packet = NULL;
	}

	INIT_LIST_HEAD(&req->list);
	req->ctx = ctx;
	req->num_hw_update_entries  = 0;
	req->num_in_map_entries     = 0;
	req->num_out_map_entries    = 0;
	req->num_out_acked          = 0;
	req->flushed                = 0;
	atomic_set(&req->num_in_acked, 0);
	memset(&req->pf_data, 0, sizeof(struct cam_hw_mgr_dump_pf_data));

	remain_len = cam_context_parse_config_cmd(ctx, cmd, &packet);
	if (IS_ERR(packet)) {
		rc = PTR_ERR(packet);
		goto free_req;
	}

	if (packet->header.request_id <= ctx->last_flush_req) {
		CAM_ERR(CAM_CORE,
			"request %lld has been flushed, reject packet",
			packet->header.request_id);
		rc = -EBADR;
		goto free_req;
	}

	if (packet->header.request_id > ctx->last_flush_req)
		ctx->last_flush_req = 0;

	/* preprocess the configuration */
	memset(&cfg, 0, sizeof(cfg));
	cfg.packet = packet;
	cfg.remain_len = remain_len;
	cfg.ctxt_to_hw_map = ctx->ctxt_to_hw_map;
	cfg.max_hw_update_entries = ctx->max_hw_update_entries;
	cfg.num_hw_update_entries = req->num_hw_update_entries;
	cfg.hw_update_entries = req->hw_update_entries;
	cfg.max_out_map_entries = ctx->max_out_map_entries;
	cfg.out_map_entries = req->out_map_entries;
	cfg.max_in_map_entries = ctx->max_in_map_entries;
	cfg.in_map_entries = req->in_map_entries;
	cfg.pf_data = &(req->pf_data);
	cfg.priv = req->req_priv;

	rc = ctx->hw_mgr_intf->hw_prepare_update(
		ctx->hw_mgr_intf->hw_mgr_priv, &cfg);
	if (rc != 0) {
		CAM_ERR(CAM_CTXT,
			"[%s][%d] Prepare config packet failed in HW layer",
			ctx->dev_name, ctx->ctx_id);
		rc = -EFAULT;
		goto free_req;
	}
	req->num_hw_update_entries = cfg.num_hw_update_entries;
	req->num_out_map_entries = cfg.num_out_map_entries;
	req->num_in_map_entries = cfg.num_in_map_entries;
	atomic_set(&req->num_in_acked, 0);
	req->request_id = packet->header.request_id;
	req->status = 1;
	req->req_priv = cfg.priv;
	req->packet = packet;

	for (i = 0; i < req->num_out_map_entries; i++) {
		rc = cam_sync_get_obj_ref(req->out_map_entries[i].sync_id);
		if (rc) {
			CAM_ERR(CAM_CTXT, "Can't get ref for sync %d",
				req->out_map_entries[i].sync_id);
			goto put_ref;
		}
	}

	if (req->num_in_map_entries > 0) {
		spin_lock(&ctx->lock);
		list_add_tail(&req->list, &ctx->pending_req_list);
		spin_unlock(&ctx->lock);

		if (cam_debug_ctx_req_list & ctx->dev_id)
			CAM_INFO(CAM_CTXT,
				"[%s][%d] : Moving req[%llu] from free_list to pending_list",
				ctx->dev_name, ctx->ctx_id, req->request_id);

		for (j = 0; j < req->num_in_map_entries; j++) {
			rc = cam_sync_check_valid(
				req->in_map_entries[j].sync_id);
			if (rc) {
				spin_lock(&ctx->lock);
				list_del_init(&req->list);
				spin_unlock(&ctx->lock);
				CAM_ERR(CAM_CTXT,
					"invalid in map sync object %d",
					req->in_map_entries[j].sync_id);
				goto put_ref;
			}
		}

		for (j = 0; j < req->num_in_map_entries; j++) {
			cam_context_getref(ctx);
			rc = cam_sync_register_callback(
					cam_context_sync_callback,
					(void *)req,
					req->in_map_entries[j].sync_id);
			if (rc) {
				CAM_ERR(CAM_CTXT,
					"[%s][%d] Failed register fence cb: %d ret = %d",
					ctx->dev_name, ctx->ctx_id,
					req->in_map_entries[j].sync_id, rc);
				spin_lock(&ctx->lock);
				list_del_init(&req->list);
				spin_unlock(&ctx->lock);

				if (cam_debug_ctx_req_list & ctx->dev_id)
					CAM_INFO(CAM_CTXT,
						"[%s][%d] : Moving req[%llu] from pending_list to free_list",
						ctx->dev_name, ctx->ctx_id,
						req->request_id);

				cam_context_putref(ctx);
				goto put_ref;
			}
			CAM_DBG(CAM_CTXT, "register in fence cb: %d ret = %d",
				req->in_map_entries[j].sync_id, rc);
		}
	}

	return rc;
put_ref:
	for (--i; i >= 0; i--) {
		if (cam_sync_put_obj_ref(req->out_map_entries[i].sync_id))
			CAM_ERR(CAM_CTXT, "Failed to put ref of fence %d",
				req->out_map_entries[i].sync_id);
	}
free_req:
	if (packet)
		cam_common_mem_free(packet);

	req->packet = NULL;
	spin_lock(&ctx->lock);
	list_add_tail(&req->list, &ctx->free_req_list);
	req->ctx = NULL;
	spin_unlock(&ctx->lock);

	return rc;
}

int32_t cam_context_acquire_dev_to_hw(struct cam_context *ctx,
	struct cam_acquire_dev_cmd *cmd)
{
	int rc;
	struct cam_hw_acquire_args param;
	struct cam_create_dev_hdl req_hdl_param;
	struct cam_hw_release_args release;

	if (!ctx || !cmd) {
		CAM_ERR(CAM_CTXT, "Invalid input params %pK %pK", ctx, cmd);
		rc = -EINVAL;
		goto end;
	}

	if (!ctx->hw_mgr_intf) {
		CAM_ERR(CAM_CTXT, "[%s][%d] HW interface is not ready",
			ctx->dev_name, ctx->ctx_id);
		rc = -EFAULT;
		goto end;
	}

	CAM_DBG(CAM_CTXT, "ses hdl: %x, num_res: %d, type: %d, res: %lld",
		cmd->session_handle, cmd->num_resources, cmd->handle_type,
		cmd->resource_hdl);

	if (cmd->num_resources > CAM_CTX_RES_MAX) {
		CAM_ERR(CAM_CTXT, "[%s][%d] resource[%d] limit exceeded",
			ctx->dev_name, ctx->ctx_id, cmd->num_resources);
		rc = -ENOMEM;
		goto end;
	}

	/* for now we only support user pointer */
	if (cmd->handle_type != 1)  {
		CAM_ERR(CAM_CTXT, "[%s][%d] Only user pointer is supported",
			ctx->dev_name, ctx->ctx_id);
		rc = -EINVAL;
		goto end;
	}

	/* fill in parameters */
	param.context_data = ctx;
	param.ctx_id = ctx->ctx_id;
	param.event_cb = ctx->irq_cb_intf;
	param.mini_dump_cb = ctx->mini_dump_cb;
	param.num_acq = cmd->num_resources;
	param.acquire_info = cmd->resource_hdl;

	/* Allocate memory for hw and map entries */
	rc = cam_context_allocate_mem_hw_entries(ctx);

	if (rc != 0) {
		CAM_ERR(CAM_CTXT, "[%s][%d] Alloc entries failed",
			ctx->dev_name, ctx->ctx_id);
		goto end;
	}

	/* call HW manager to reserve the resource */
	rc = ctx->hw_mgr_intf->hw_acquire(ctx->hw_mgr_intf->hw_mgr_priv,
		&param);
	if (rc != 0) {
		CAM_ERR(CAM_CTXT, "[%s][%d] Acquire device failed",
			ctx->dev_name, ctx->ctx_id);
		goto end;
	}

	ctx->ctxt_to_hw_map = param.ctxt_to_hw_map;
	ctx->hw_mgr_ctx_id = param.hw_mgr_ctx_id;

	snprintf(ctx->ctx_id_string, sizeof(ctx->ctx_id_string),
		"%s_ctx[%d]_hwmgrctx[%d]_Done",
		ctx->dev_name,
		ctx->ctx_id,
		ctx->hw_mgr_ctx_id);

	/* if hw resource acquire successful, acquire dev handle */
	req_hdl_param.session_hdl = cmd->session_handle;
	/* bridge is not ready for these flags. so false for now */
	req_hdl_param.v4l2_sub_dev_flag = 0;
	req_hdl_param.media_entity_flag = 0;
	req_hdl_param.priv = ctx;
	req_hdl_param.ops = ctx->crm_ctx_intf;
	req_hdl_param.dev_id = ctx->dev_id;
	ctx->dev_hdl = cam_create_device_hdl(&req_hdl_param);
	if (ctx->dev_hdl <= 0) {
		rc = -EFAULT;
		CAM_ERR(CAM_CTXT, "[%s][%d] Can not create device handle",
			ctx->dev_name, ctx->ctx_id);
		goto free_hw;
	}
	cmd->dev_handle = ctx->dev_hdl;

	/* store session information */
	ctx->session_hdl = cmd->session_handle;

	return rc;

free_hw:
	release.ctxt_to_hw_map = ctx->ctxt_to_hw_map;
	ctx->hw_mgr_intf->hw_release(ctx->hw_mgr_intf->hw_mgr_priv, &release);
	ctx->ctxt_to_hw_map = NULL;
	ctx->dev_hdl = -1;
end:
	return rc;
}

int32_t cam_context_flush_ctx_to_hw(struct cam_context *ctx)
{
	struct cam_hw_flush_args flush_args;
	struct list_head temp_list;
	struct list_head *list;
	struct cam_ctx_request *req;
	uint32_t i, num_entries = 0;
	int rc = 0;
	bool free_req;

	CAM_DBG(CAM_CTXT, "[%s] E: NRT flush ctx", ctx->dev_name);
	memset(&flush_args, 0, sizeof(flush_args));

	/*
	 * flush pending requests, take the sync lock to synchronize with the
	 * sync callback thread so that the sync cb thread does not try to
	 * submit request to h/w while the request is being flushed
	 */
	mutex_lock(&ctx->sync_mutex);
	INIT_LIST_HEAD(&temp_list);
	spin_lock(&ctx->lock);
	list_splice_init(&ctx->pending_req_list, &temp_list);
	spin_unlock(&ctx->lock);

	if (cam_debug_ctx_req_list & ctx->dev_id)
		CAM_INFO(CAM_CTXT,
			"[%s][%d] : Moving all pending requests from pending_list to temp_list",
			ctx->dev_name, ctx->ctx_id);

	flush_args.num_req_pending = 0;
	flush_args.last_flush_req = ctx->last_flush_req;
	list_for_each(list, &temp_list) {
		num_entries++;
	}
	if (num_entries) {
		flush_args.flush_req_pending =
			kcalloc(num_entries, sizeof(void *), GFP_KERNEL);
		if (!flush_args.flush_req_pending) {
			CAM_ERR(CAM_CTXT, "[%s][%d] : Flush array memory alloc fail",
				ctx->dev_name, ctx->ctx_id);
			mutex_unlock(&ctx->sync_mutex);
			rc = -ENOMEM;
			goto end;
		}
	}

	while (num_entries) {

		if (list_empty(&temp_list))
			break;

		req = list_first_entry(&temp_list,
				struct cam_ctx_request, list);

		list_del_init(&req->list);
		req->flushed = 1;

		flush_args.flush_req_pending[flush_args.num_req_pending++] =
			req->req_priv;

		free_req = false;
		for (i = 0; i < req->num_in_map_entries; i++) {
			rc = cam_sync_deregister_callback(
				cam_context_sync_callback,
				(void *)req,
				req->in_map_entries[i].sync_id);
			if (!rc) {
				cam_context_putref(ctx);
				if (atomic_inc_return(&req->num_in_acked) ==
					req->num_in_map_entries)
					free_req = true;
			}
		}

		for (i = 0; i < req->num_out_map_entries; i++) {
			if (req->out_map_entries[i].sync_id != -1) {
				rc = cam_sync_signal(
					req->out_map_entries[i].sync_id,
					CAM_SYNC_STATE_SIGNALED_CANCEL,
					CAM_SYNC_COMMON_EVENT_FLUSH);
				if (rc == -EALREADY) {
					CAM_ERR(CAM_CTXT,
					"Req: %llu already signalled, sync_id:%d",
					req->request_id,
					req->out_map_entries[i].sync_id);
					break;
				}
			}
		}

		/*
		 * If we have deregistered the last sync callback, req will
		 * not be put on the free list. So put it on the free list here
		 */
		if (free_req) {
			req->ctx = NULL;
			if (req->packet) {
				cam_common_mem_free(req->packet);
				req->packet = NULL;
			}
			spin_lock(&ctx->lock);
			list_add_tail(&req->list, &ctx->free_req_list);
			spin_unlock(&ctx->lock);
		}

		if (cam_debug_ctx_req_list & ctx->dev_id)
			CAM_INFO(CAM_CTXT,
				"[%s][%d] : Deleting req[%llu] from temp_list",
				ctx->dev_name, ctx->ctx_id, req->request_id);
	}
	mutex_unlock(&ctx->sync_mutex);

	INIT_LIST_HEAD(&temp_list);
	spin_lock(&ctx->lock);
	list_splice_init(&ctx->active_req_list, &temp_list);
	spin_unlock(&ctx->lock);

	if (ctx->hw_mgr_intf->hw_flush) {
		flush_args.num_req_active = 0;
		num_entries = 0;
		list_for_each(list, &temp_list) {
			num_entries++;
		}
		if (num_entries) {
			flush_args.flush_req_active =
				kcalloc(num_entries, sizeof(void *), GFP_KERNEL);
			if (!flush_args.flush_req_active) {
				CAM_ERR(CAM_CTXT, "[%s][%d] : Flush array memory alloc fail",
					ctx->dev_name, ctx->ctx_id);
				rc = -ENOMEM;
				goto end;
			}

			list_for_each_entry(req, &temp_list, list) {
				flush_args.flush_req_active[flush_args.num_req_active++] =
					req->req_priv;
			}
		}

		if (flush_args.num_req_pending || flush_args.num_req_active) {
			flush_args.ctxt_to_hw_map = ctx->ctxt_to_hw_map;
			flush_args.flush_type = CAM_FLUSH_TYPE_ALL;
			ctx->hw_mgr_intf->hw_flush(
				ctx->hw_mgr_intf->hw_mgr_priv, &flush_args);
		}
	}

	if (cam_debug_ctx_req_list & ctx->dev_id)
		CAM_INFO(CAM_CTXT,
			"[%s][%d] : Moving all requests from active_list to temp_list",
			ctx->dev_name, ctx->ctx_id);

	while (num_entries) {

		if (list_empty(&temp_list))
			break;

		req = list_first_entry(&temp_list,
			struct cam_ctx_request, list);
		list_del_init(&req->list);

		for (i = 0; i < req->num_out_map_entries; i++) {
			if (req->out_map_entries[i].sync_id != -1) {
				rc = cam_sync_signal(
					req->out_map_entries[i].sync_id,
					CAM_SYNC_STATE_SIGNALED_CANCEL,
					CAM_SYNC_COMMON_EVENT_FLUSH);
				if (rc == -EALREADY) {
					CAM_ERR(CAM_CTXT,
						"Req: %llu already signalled ctx: %pK dev_name: %s dev_handle: %d ctx_state: %d",
						req->request_id, req->ctx,
						req->ctx->dev_name,
						req->ctx->dev_hdl,
						req->ctx->state);
					break;
				}
			}
		}

		if (req->packet) {
			cam_common_mem_free(req->packet);
			req->packet = NULL;
		}
		spin_lock(&ctx->lock);
		list_add_tail(&req->list, &ctx->free_req_list);
		spin_unlock(&ctx->lock);
		req->ctx = NULL;

		if (cam_debug_ctx_req_list & ctx->dev_id)
			CAM_INFO(CAM_CTXT,
				"[%s][%d] : Moving req[%llu] from temp_list to free_list",
				ctx->dev_name, ctx->ctx_id, req->request_id);
	}

	rc = 0;
	CAM_DBG(CAM_CTXT, "[%s] X: NRT flush ctx", ctx->dev_name);

end:
	kfree(flush_args.flush_req_active);
	kfree(flush_args.flush_req_pending);
	return rc;
}

int32_t cam_context_flush_req_to_hw(struct cam_context *ctx,
	struct cam_flush_dev_cmd *cmd)
{
	struct cam_ctx_request *req = NULL;
	struct cam_hw_flush_args flush_args;
	uint32_t i = 0;
	int32_t sync_id = 0;
	int rc = 0;
	bool free_req = false;

	CAM_DBG(CAM_CTXT, "[%s] E: NRT flush req", ctx->dev_name);

	memset(&flush_args, 0, sizeof(flush_args));
	flush_args.num_req_pending = 0;
	flush_args.num_req_active = 0;
	flush_args.flush_req_pending = kzalloc(sizeof(void *), GFP_KERNEL);
	if (!flush_args.flush_req_pending) {
		CAM_ERR(CAM_CTXT, "[%s][%d] : Flush array memory alloc fail",
			ctx->dev_name, ctx->ctx_id);
		rc = -ENOMEM;
		goto end;
	}
	mutex_lock(&ctx->sync_mutex);
	spin_lock(&ctx->lock);
	list_for_each_entry(req, &ctx->pending_req_list, list) {
		if (req->request_id != cmd->req_id)
			continue;

		if (cam_debug_ctx_req_list & ctx->dev_id)
			CAM_INFO(CAM_CTXT,
				"[%s][%d] : Deleting req[%llu] from pending_list",
				ctx->dev_name, ctx->ctx_id, req->request_id);

		list_del_init(&req->list);
		req->flushed = 1;

		flush_args.flush_req_pending[flush_args.num_req_pending++] =
			req->req_priv;
		break;
	}
	spin_unlock(&ctx->lock);
	mutex_unlock(&ctx->sync_mutex);

	if (ctx->hw_mgr_intf->hw_flush) {
		if (!flush_args.num_req_pending) {
			flush_args.flush_req_active = kzalloc(sizeof(void *), GFP_KERNEL);
			if (!flush_args.flush_req_active) {
				CAM_ERR(CAM_CTXT, "[%s][%d] : Flush array memory alloc fail",
					ctx->dev_name, ctx->ctx_id);
				rc = -ENOMEM;
				goto end;
			}
			spin_lock(&ctx->lock);
			list_for_each_entry(req, &ctx->active_req_list, list) {
				if (req->request_id != cmd->req_id)
					continue;

				list_del_init(&req->list);

				flush_args.flush_req_active[
					flush_args.num_req_active++] =
					req->req_priv;
				break;
			}
			spin_unlock(&ctx->lock);
		}

		if (flush_args.num_req_pending || flush_args.num_req_active) {
			flush_args.ctxt_to_hw_map = ctx->ctxt_to_hw_map;
			flush_args.flush_type = CAM_FLUSH_TYPE_REQ;
			ctx->hw_mgr_intf->hw_flush(
				ctx->hw_mgr_intf->hw_mgr_priv, &flush_args);
		}
	}

	if (req) {
		if (flush_args.num_req_pending) {
			for (i = 0; i < req->num_in_map_entries; i++) {
				rc = cam_sync_deregister_callback(
					cam_context_sync_callback,
					(void *)req,
					req->in_map_entries[i].sync_id);
				if (rc)
					continue;

				cam_context_putref(ctx);
				if (atomic_inc_return(&req->num_in_acked) ==
					req->num_in_map_entries)
					free_req = true;
			}
		}

		if (flush_args.num_req_pending || flush_args.num_req_active) {
			for (i = 0; i < req->num_out_map_entries; i++) {
				sync_id =
					req->out_map_entries[i].sync_id;
				if (sync_id != -1) {
					rc = cam_sync_signal(sync_id,
						CAM_SYNC_STATE_SIGNALED_CANCEL,
						CAM_SYNC_COMMON_EVENT_FLUSH);
					if (rc == -EALREADY) {
						CAM_ERR(CAM_CTXT,
						"Req: %llu already signalled, sync_id:%d",
						req->request_id, sync_id);
						break;
					}
				}
			}
			if (flush_args.num_req_active || free_req) {
				req->ctx = NULL;
				if (req->packet) {
					cam_common_mem_free(req->packet);
					req->packet = NULL;
				}
				spin_lock(&ctx->lock);
				list_add_tail(&req->list, &ctx->free_req_list);
				spin_unlock(&ctx->lock);

				if (cam_debug_ctx_req_list & ctx->dev_id)
					CAM_INFO(CAM_CTXT,
						"[%s][%d] : Moving req[%llu] from %s to free_list",
						ctx->dev_name, ctx->ctx_id,
						req->request_id,
						flush_args.num_req_active ?
							"active_list" :
							"pending_list");
			}
		}
	}

	rc = 0;
	CAM_DBG(CAM_CTXT, "[%s] X: NRT flush req", ctx->dev_name);

end:
	kfree(flush_args.flush_req_active);
	kfree(flush_args.flush_req_pending);
	return rc;
}

int32_t cam_context_flush_dev_to_hw(struct cam_context *ctx,
	struct cam_flush_dev_cmd *cmd)
{

	int rc = 0;

	if (!ctx || !cmd) {
		CAM_ERR(CAM_CTXT, "Invalid input params %pK %pK", ctx, cmd);
		rc = -EINVAL;
		goto end;
	}

	if (!ctx->hw_mgr_intf) {
		CAM_ERR(CAM_CTXT, "[%s][%d] HW interface is not ready",
			ctx->dev_name, ctx->ctx_id);
		rc = -EFAULT;
		goto end;
	}

	if (cmd->flush_type == CAM_FLUSH_TYPE_ALL) {
		ctx->last_flush_req = cmd->req_id;
		rc = cam_context_flush_ctx_to_hw(ctx);
	} else if (cmd->flush_type == CAM_FLUSH_TYPE_REQ)
		rc = cam_context_flush_req_to_hw(ctx, cmd);
	else {
		rc = -EINVAL;
		CAM_ERR(CAM_CORE, "[%s][%d] Invalid flush type %d",
			ctx->dev_name, ctx->ctx_id, cmd->flush_type);
	}

end:
	return rc;
}

int32_t cam_context_start_dev_to_hw(struct cam_context *ctx,
	struct cam_start_stop_dev_cmd *cmd)
{
	int rc = 0;
	struct cam_hw_start_args arg;

	if (!ctx || !cmd) {
		CAM_ERR(CAM_CTXT, "Invalid input params %pK %pK", ctx, cmd);
		rc = -EINVAL;
		goto end;
	}

	if (!ctx->hw_mgr_intf) {
		CAM_ERR(CAM_CTXT, "[%s][%d] HW interface is not ready",
			ctx->dev_name, ctx->ctx_id);
		rc = -EFAULT;
		goto end;
	}

	if ((cmd->session_handle != ctx->session_hdl) ||
		(cmd->dev_handle != ctx->dev_hdl)) {
		CAM_ERR(CAM_CTXT,
			"[%s][%d] Invalid session hdl[%d], dev_handle[%d]",
			ctx->dev_name, ctx->ctx_id,
			cmd->session_handle, cmd->dev_handle);
		rc = -EPERM;
		goto end;
	}

	if (ctx->hw_mgr_intf->hw_start) {
		arg.ctxt_to_hw_map = ctx->ctxt_to_hw_map;
		rc = ctx->hw_mgr_intf->hw_start(ctx->hw_mgr_intf->hw_mgr_priv,
				&arg);
		if (rc) {
			/* HW failure. user need to clean up the resource */
			CAM_ERR(CAM_CTXT, "[%s][%d] Start HW failed",
				ctx->dev_name, ctx->ctx_id);
			goto end;
		}
	}

end:
	return rc;
}

int32_t cam_context_stop_dev_to_hw(struct cam_context *ctx)
{
	int rc = 0;
	struct cam_hw_stop_args stop;

	if (!ctx) {
		CAM_ERR(CAM_CTXT, "Invalid input param");
		rc = -EINVAL;
		goto end;
	}

	if (!ctx->hw_mgr_intf) {
		CAM_ERR(CAM_CTXT, "[%s][%d] HW interface is not ready",
			ctx->dev_name, ctx->ctx_id);
		rc = -EFAULT;
		goto end;
	}

	rc = cam_context_validate_thread();
	if (rc)
		goto end;

	rc = cam_context_flush_ctx_to_hw(ctx);
	if (rc)
		goto end;

	/* stop hw first */
	if (ctx->hw_mgr_intf->hw_stop) {
		stop.ctxt_to_hw_map = ctx->ctxt_to_hw_map;
		ctx->hw_mgr_intf->hw_stop(ctx->hw_mgr_intf->hw_mgr_priv,
			&stop);
	}

end:
	return rc;
}

int32_t cam_context_dump_pf_info_to_hw(struct cam_context *ctx,
	struct cam_hw_mgr_dump_pf_data *pf_data, bool *mem_found, bool *ctx_found,
	uint32_t  *resource_type, struct cam_smmu_pf_info *pf_info)
{
	int rc = 0;
	struct cam_hw_cmd_args cmd_args;

	if (!ctx) {
		CAM_ERR(CAM_CTXT, "Invalid input params %pK ", ctx);
		rc = -EINVAL;
		goto end;
	}

	if (!ctx->hw_mgr_intf) {
		CAM_ERR(CAM_CTXT, "[%s][%d] HW interface is not ready",
			ctx->dev_name, ctx->ctx_id);
		rc = -EFAULT;
		goto end;
	}

	if (ctx->hw_mgr_intf->hw_cmd) {
		cmd_args.ctxt_to_hw_map = ctx->ctxt_to_hw_map;
		cmd_args.cmd_type = CAM_HW_MGR_CMD_DUMP_PF_INFO;
		cmd_args.u.pf_args.pf_data = *pf_data;
		cmd_args.u.pf_args.iova = pf_info->iova;
		cmd_args.u.pf_args.buf_info = pf_info->buf_info;
		cmd_args.u.pf_args.mem_found = mem_found;
		cmd_args.u.pf_args.ctx_found = ctx_found;
		cmd_args.u.pf_args.resource_type = resource_type;
		cmd_args.u.pf_args.bid = pf_info->bid;
		cmd_args.u.pf_args.pid = pf_info->pid;
		cmd_args.u.pf_args.mid = pf_info->mid;
		ctx->hw_mgr_intf->hw_cmd(ctx->hw_mgr_intf->hw_mgr_priv,
			&cmd_args);
	}

end:
	return rc;
}

int32_t cam_context_dump_hw_acq_info(struct cam_context *ctx)
{
	int rc = 0;
	struct cam_hw_cmd_args cmd_args;

	if (!ctx) {
		CAM_ERR(CAM_CTXT, "Invalid input params");
		rc = -EINVAL;
		goto end;
	}

	if (!ctx->hw_mgr_intf) {
		CAM_ERR(CAM_CTXT, "[%s][%d] HW interface is not ready",
			ctx->dev_name, ctx->ctx_id);
		rc = -EFAULT;
		goto end;
	}

	if (ctx->hw_mgr_intf->hw_cmd) {
		cmd_args.ctxt_to_hw_map = ctx->ctxt_to_hw_map;
		cmd_args.cmd_type = CAM_HW_MGR_CMD_DUMP_ACQ_INFO;
		ctx->hw_mgr_intf->hw_cmd(ctx->hw_mgr_intf->hw_mgr_priv,
			&cmd_args);
	}

end:
	return rc;
}

static int cam_context_dump_context(struct cam_context *ctx,
	struct cam_hw_dump_args *dump_args)
{
	int                             rc;
	int                             i;
	size_t                          buf_len;
	size_t                          remain_len;
	uint8_t                        *dst;
	uint64_t                       *addr, *start;
	uint32_t                        min_len;
	uintptr_t                       cpu_addr;
	struct cam_ctx_request         *req;
	struct cam_context_dump_header *hdr;

	if (!ctx || !dump_args) {
		CAM_ERR(CAM_CORE, "Invalid parameters %pK %pK",
			ctx, dump_args);
		return -EINVAL;
	}

	spin_lock_bh(&ctx->lock);
	if (list_empty(&ctx->active_req_list)) {
		CAM_ERR(CAM_CTXT, "[%s][%d] no active request",
			ctx->dev_name, ctx->ctx_id);
		spin_unlock_bh(&ctx->lock);
		return -EIO;
	}
	req = list_first_entry(&ctx->active_req_list,
		struct cam_ctx_request, list);
	spin_unlock_bh(&ctx->lock);
	rc  = cam_mem_get_cpu_buf(dump_args->buf_handle,
		&cpu_addr, &buf_len);
	if (rc) {
		CAM_ERR(CAM_CTXT, "Invalid hdl %u rc %d",
			dump_args->buf_handle, rc);
		return rc;
	}
	if (dump_args->offset >= buf_len) {
		CAM_WARN(CAM_CTXT, "dump buffer overshoot offset %zu len %zu",
			dump_args->offset, buf_len);
		cam_mem_put_cpu_buf(dump_args->buf_handle);
		return -ENOSPC;
	}

	remain_len = buf_len - dump_args->offset;
	min_len =  sizeof(struct cam_context_dump_header) +
		    (CAM_CTXT_DUMP_NUM_WORDS + req->num_in_map_entries +
		    (req->num_out_map_entries * 2)) * sizeof(uint64_t);

	if (remain_len < min_len) {
		CAM_WARN(CAM_CTXT, "dump buffer exhaust remain %zu min %u",
			remain_len, min_len);
		cam_mem_put_cpu_buf(dump_args->buf_handle);
		return -ENOSPC;
	}
	dst = (uint8_t *)cpu_addr + dump_args->offset;
	hdr = (struct cam_context_dump_header *)dst;
	scnprintf(hdr->tag, CAM_CTXT_DUMP_TAG_MAX_LEN,
		"%s_CTXT_DUMP:", ctx->dev_name);
	hdr->word_size = sizeof(uint64_t);
	addr = (uint64_t *)(dst + sizeof(struct cam_context_dump_header));
	start = addr;
	*addr++ = ctx->ctx_id;
	*addr++ = refcount_read(&(ctx->refcount.refcount));
	*addr++ = ctx->last_flush_req;
	*addr++ = ctx->state;
	*addr++ = req->num_out_map_entries;
	for (i = 0; i < req->num_out_map_entries; i++) {
		*addr++ = req->out_map_entries[i].resource_handle;
		*addr++ = req->out_map_entries[i].sync_id;
	}
	*addr++ = req->num_in_map_entries;
	for (i = 0; i < req->num_in_map_entries; i++)
		*addr++ = req->in_map_entries[i].sync_id;
	hdr->size = hdr->word_size * (addr - start);
	dump_args->offset += hdr->size +
		sizeof(struct cam_context_dump_header);
	cam_mem_put_cpu_buf(dump_args->buf_handle);
	return 0;
}

int32_t cam_context_dump_dev_to_hw(struct cam_context *ctx,
	struct cam_dump_req_cmd *cmd)
{
	int                     rc = 0;
	struct cam_hw_dump_args dump_args;

	if (!ctx || !cmd) {
		CAM_ERR(CAM_CTXT, "Invalid input params %pK %pK", ctx, cmd);
		return -EINVAL;
	}
	if (!ctx->hw_mgr_intf) {
		CAM_ERR(CAM_CTXT, "[%s][%d] HW interface is not ready",
			ctx->dev_name, ctx->ctx_id);
		return -EFAULT;
	}
	memset(&dump_args, 0, sizeof(dump_args));
	if (ctx->hw_mgr_intf->hw_dump) {
		dump_args.ctxt_to_hw_map = ctx->ctxt_to_hw_map;
		dump_args.buf_handle = cmd->buf_handle;
		dump_args.offset = cmd->offset;
		dump_args.request_id = cmd->issue_req_id;
		dump_args.error_type = cmd->error_type;
		rc  = ctx->hw_mgr_intf->hw_dump(
			ctx->hw_mgr_intf->hw_mgr_priv,
			&dump_args);
		if (rc) {
			CAM_ERR(CAM_CTXT, "[%s][%d] handle[%u] failed",
			    ctx->dev_name, ctx->ctx_id, dump_args.buf_handle);
			return rc;
		}
		/* Offset will change if the issue request id is found with
		 * the hw and has been lying with it beyond threshold time.
		 * If offset does not change, do not dump the context
		 * information as the current context has no problem with
		 * the provided request id.
		 */
		if (dump_args.offset > cmd->offset) {
			cam_context_dump_context(ctx, &dump_args);
			CAM_INFO(CAM_CTXT, "[%s] ctx: %d Filled Length %u",
				 ctx->dev_name, ctx->ctx_id,
				 dump_args.offset - cmd->offset);
			cmd->offset  = dump_args.offset;
		}
	} else {
		CAM_DBG(CAM_CTXT, "%s hw dump not registered", ctx->dev_name);
	}
	return rc;
}

size_t cam_context_parse_config_cmd(struct cam_context *ctx, struct cam_config_dev_cmd *cmd,
	struct cam_packet **packet)
{
	size_t len;
	uintptr_t packet_addr;
	int rc = 0;
	struct cam_packet *packet_u;
	size_t packet_len = 0;

	if (!ctx || !cmd || !packet) {
		CAM_ERR(CAM_CTXT, "invalid args");
		return  -EINVAL;
	}

	/* for config dev, only memory handle is supported */
	/* map packet from the memhandle */
	rc = cam_mem_get_cpu_buf((int32_t) cmd->packet_handle, &packet_addr, &len);
	if (rc != 0) {
		CAM_ERR(CAM_CTXT, "[%s][%d] Can not get packet address for handle:%llx",
			ctx->dev_name, ctx->ctx_id, cmd->packet_handle);
		return  -EINVAL;
	}

	if ((len < sizeof(struct cam_packet)) ||
		((size_t)cmd->offset >= len - sizeof(struct cam_packet))) {
		CAM_ERR(CAM_CTXT, "invalid buff length: %zu or offset: %zu", len,
			(size_t)cmd->offset);
		rc = -EINVAL;
		goto err;
	}

	packet_u = (struct cam_packet *) ((uint8_t *)packet_addr + (uint32_t)cmd->offset);
	if (IS_ERR_OR_NULL(packet_u)) {
		rc = PTR_ERR(packet_u);
		goto put_cpu_buf;
	}

	packet_len = len - (size_t)cmd->offset;
	rc = cam_packet_util_copy_pkt_to_kmd(packet_u, packet, packet_len);
	if (rc) {
		CAM_ERR(CAM_CTXT, "Copying packet to KMD failed");
		goto put_cpu_buf;
	}

	CAM_DBG(CAM_CTXT,
		"handle:%llx, addr:0x%zx, offset:%0xllx, len:%zu, req:%llu, size:%u, opcode:0x%x",
		cmd->packet_handle, packet_addr, cmd->offset, len, (*packet)->header.request_id,
		(*packet)->header.size, (*packet)->header.op_code);

	cam_mem_put_cpu_buf((int32_t) cmd->packet_handle);
	return (len - (size_t)cmd->offset);

err:
	if (packet)
		*packet = ERR_PTR(rc);
put_cpu_buf:
	if (cmd)
		cam_mem_put_cpu_buf((int32_t) cmd->packet_handle);
	return 0;
}

static void __cam_context_req_mini_dump(struct cam_ctx_request *req,
	uint8_t *start_addr, uint8_t *end_addr,
	unsigned long *bytes_updated)
{
	struct cam_hw_req_mini_dump      *req_md;
	struct cam_buf_io_cfg            *io_cfg;
	struct cam_packet                *packet = NULL;
	unsigned long                     bytes_written = 0;
	unsigned long                     bytes_required = 0;

	bytes_required = sizeof(*req_md);
	if (start_addr + bytes_written + bytes_required > end_addr)
		goto end;

	req_md = (struct cam_hw_req_mini_dump *)start_addr;

	req_md->num_fence_map_in = req->num_in_map_entries;
	req_md->num_fence_map_out = req->num_out_map_entries;
	req_md->num_in_acked = atomic_read(&req->num_in_acked);
	req_md->num_out_acked = req->num_out_acked;
	req_md->request_id = req->request_id;
	bytes_written += bytes_required;

	if (req->num_out_map_entries) {
		bytes_required = sizeof(struct cam_hw_fence_map_entry) *
					req->num_out_map_entries;
		if (start_addr + bytes_written + bytes_required > end_addr)
			goto end;

		req_md->fence_map_out = (struct cam_hw_fence_map_entry *)
				(start_addr + bytes_written);
		memcpy(req_md->fence_map_out, req->out_map_entries, bytes_required);
		req_md->num_fence_map_out = req->num_out_map_entries;
		bytes_written += bytes_required;
	}

	if (req->num_in_map_entries) {
		bytes_required = sizeof(struct cam_hw_fence_map_entry) *
				    req->num_in_map_entries;
		if (start_addr + bytes_written + bytes_required > end_addr)
			goto end;

		req_md->fence_map_in = (struct cam_hw_fence_map_entry *)
				(start_addr + bytes_written);
		memcpy(req_md->fence_map_in, req->in_map_entries, bytes_required);
		req_md->num_fence_map_in = req->num_in_map_entries;
		bytes_written += bytes_required;
	}

	packet = (struct cam_packet *)req->pf_data.packet;

	if (packet && packet->num_io_configs) {
		bytes_required = packet->num_io_configs * sizeof(struct cam_buf_io_cfg);
		if (start_addr + bytes_written + bytes_required > end_addr)
			goto end;

		io_cfg = (struct cam_buf_io_cfg *)((uint32_t *)&packet->payload +
			    packet->io_configs_offset / 4);
		req_md->io_cfg = (struct cam_buf_io_cfg *)(start_addr + bytes_written);
		memcpy(req_md->io_cfg, io_cfg, bytes_required);
		bytes_written += bytes_required;
		req_md->num_io_cfg = packet->num_io_configs;
	}
end:
	*bytes_updated = bytes_written;
}

int cam_context_mini_dump(struct cam_context *ctx, void *args)
{
	struct cam_hw_mini_dump_info *md;
	struct cam_ctx_request *req, *req_temp;
	struct cam_hw_mini_dump_args *md_args;
	uint8_t                      *start_addr;
	uint8_t                      *end_addr;
	unsigned long                 bytes_written = 0;
	unsigned long                 bytes_updated = 0;

	if (!ctx || !args) {
		CAM_ERR(CAM_CTXT, "invalid params");
		return -EINVAL;
	}

	md_args = (struct cam_hw_mini_dump_args *)args;
	if (md_args->len < sizeof(*md)) {
		md_args->bytes_written = 0;
		CAM_ERR(CAM_CTXT, "Insufficient len %lu, bytes_written %lu", md_args->len,
			md_args->bytes_written);
		return 0;
	}

	start_addr = (uint8_t *)md_args->start_addr;
	end_addr  = start_addr + md_args->len;
	md = (struct cam_hw_mini_dump_info *)md_args->start_addr;
	md->ctx_id = ctx->ctx_id;
	md->last_flush_req = ctx->last_flush_req;
	md->hw_mgr_ctx_id = ctx->hw_mgr_ctx_id;
	md->dev_id = ctx->dev_id;
	md->link_hdl = ctx->link_hdl;
	md->state = ctx->state;
	md->session_hdl = ctx->session_hdl;
	md->dev_hdl = ctx->dev_hdl;
	scnprintf(md->name, CAM_HW_MINI_DUMP_DEV_NAME_LEN, ctx->dev_name);
	bytes_written += sizeof(*md);

	if (!list_empty(&ctx->active_req_list)) {
		md->active_list = (struct cam_hw_req_mini_dump *)
			    (start_addr + bytes_written);
		list_for_each_entry_safe(req, req_temp, &ctx->active_req_list, list) {
			bytes_updated = 0;
			__cam_context_req_mini_dump(req,
				(uint8_t *)&md->active_list[md->active_cnt++],
				end_addr, &bytes_updated);
			if ((start_addr + bytes_written + bytes_updated >= end_addr))
				goto end;
			bytes_written += bytes_updated;
		}
	}

	if (!list_empty(&ctx->wait_req_list)) {
		md->wait_list = (struct cam_hw_req_mini_dump *)
			    (start_addr + bytes_written);
		list_for_each_entry_safe(req, req_temp, &ctx->wait_req_list, list) {
			bytes_updated = 0;
			__cam_context_req_mini_dump(req,
				(uint8_t *)&md->wait_list[md->wait_cnt++],
				end_addr, &bytes_updated);
			if ((start_addr + bytes_written + bytes_updated >= end_addr))
				goto end;
			bytes_written += bytes_updated;
		}
	}

	if (!list_empty(&ctx->pending_req_list)) {
		md->pending_list = (struct cam_hw_req_mini_dump *)
			    (start_addr + bytes_written);
		list_for_each_entry_safe(req, req_temp, &ctx->pending_req_list, list) {
			bytes_updated = 0;
			__cam_context_req_mini_dump(req,
				(uint8_t *)&md->pending_list[md->pending_cnt++],
				end_addr, &bytes_updated);
			if ((start_addr + bytes_written + bytes_updated >= end_addr))
				goto end;
			bytes_written += bytes_updated;
		}
	}
end:
	md_args->bytes_written = bytes_written;
	CAM_INFO(CAM_CTXT, "Ctx %s bytes_written %lu", ctx->dev_name, md_args->bytes_written);
	return 0;
}
