#pragma once
#include <obs.h>
#include <string.h>
#include <stdbool.h>
#include <stdio.h>
#include "sight_snapshot_v1.h"

/* Sight owns the model/worker and the tracking bus. This private source has
 * only a proc handler; it does not perform inference or render video. */
static inline obs_source_t *hs_sight_reader_create(void)
{
    /* Check source registration before creating a private instance: unknown IDs can return invalid sources. */
    if (!obs_source_get_display_name("sight_tracking_receiver_filter"))
        return NULL;
    return obs_source_create_private("sight_tracking_receiver_filter",
                                     "HyperShader Sight snapshot reader", NULL);
}

/* Properties/control thread only. Ask the same provider that owns
 * snapshot reads for its configured endpoint inventory. Older Sight
 * versions simply lack the optional proc and continue to work manually. */
static inline bool hs_sight_populate_channels(obs_property_t *list, const char *saved)
{
    obs_property_list_clear(list);
    bool found = saved && !*saved, available = false;
    if (found) obs_property_list_add_string(list, "Default face channel", "");
    obs_source_t *reader = hs_sight_reader_create();
    obs_data_t *inventory = obs_data_create();
    if (reader) {
        proc_handler_t *handler = obs_source_get_proc_handler(reader);
        calldata_t args;
        calldata_init(&args);
        calldata_set_ptr(&args, "inventory", inventory);
        available = handler && proc_handler_call(handler, "sight_tracking_list_v1", &args) &&
            obs_data_get_int(inventory, "version") == 1;
        calldata_free(&args);
        obs_source_release(reader);
    }
    if (available) {
        obs_data_array_t *rows = obs_data_get_array(inventory, "channels");
        size_t count = rows ? obs_data_array_count(rows) : 0;
        for (size_t i = 0; i < count; ++i) {
            obs_data_t *row = obs_data_array_item(rows, i);
            const char *channel = obs_data_get_string(row, "channel");
            const char *label = obs_data_get_string(row, "label");
            const long long kind = obs_data_get_int(row, "kind");
            /* Until the shader snapshot reader supports COCO WholeBody,
             * do not offer its distinct topology as a compatible input. */
            if (*channel && strlen(channel) <= 96 &&
                kind >= SIGHT_SNAPSHOT_FACE && kind <= SIGHT_SNAPSHOT_HAND) {
                size_t index = obs_property_list_add_string(list, *label ? label : channel, channel);
                if (obs_data_get_bool(row, "ambiguous"))
                    obs_property_list_item_disable(list, index, true);
                if (saved && !strcmp(channel, saved)) found = true;
            }
            obs_data_release(row);
        }
        if (rows) obs_data_array_release(rows);
    }
    obs_data_release(inventory);
    if (!found) {
        const char *key = saved && *saved ? saved : "face";
        char label[160];
        snprintf(label, sizeof(label), "%s (saved; unavailable)", key);
        size_t index = obs_property_list_add_string(list, label, key);
        obs_property_list_item_disable(list, index, true);
    }
    return available;
}

static inline void hs_sight_snapshot_clear(sight_snapshot_v1 *out)
{
    if (!out) return;
    memset(out, 0, sizeof(*out));
    out->abi_version = SIGHT_SNAPSHOT_ABI_V1;
    out->struct_size = sizeof(*out);
}

static inline bool hs_sight_reader_read(obs_source_t *reader, const char *channel,
                                         int max_age_ms, sight_snapshot_v1 *out)
{
    if (!out) return false;
    hs_sight_snapshot_clear(out);
    if (!reader || !channel || !*channel || max_age_ms < 1 || max_age_ms > 5000)
        return false;

    proc_handler_t *handler = obs_source_get_proc_handler(reader);
    if (!handler) return false;

    calldata_t params;
    calldata_init(&params);
    calldata_set_string(&params, "channel", channel);
    calldata_set_int(&params, "max_age_ms", max_age_ms);
    calldata_set_ptr(&params, "snapshot", out);
    const bool called = proc_handler_call(handler, "sight_tracking_read_v1", &params);
    bool available = false;
    const bool success = called && calldata_get_bool(&params, "available", &available) && available;
    calldata_free(&params);

    const bool valid = success &&
        out->abi_version == SIGHT_SNAPSHOT_ABI_V1 && out->struct_size == sizeof(*out) &&
        out->kind >= SIGHT_SNAPSHOT_FACE && out->kind <= SIGHT_SNAPSHOT_HAND &&
        out->point_count <= SIGHT_SNAPSHOT_MAX_POINTS &&
        out->hands_count <= SIGHT_SNAPSHOT_MAX_HANDS &&
        out->blendshape_count <= SIGHT_SNAPSHOT_MAX_BLENDSHAPES &&
        out->sequence != 0 && out->source_width > 0 && out->source_height > 0;
    if (!valid) hs_sight_snapshot_clear(out);
    return valid;
}

#include "sight_matte_v1.h"
#include "sight_depth_v1.h"
#include "sight_motion_v1.h"
#include "sight_text_mask.h"
#include "sight_semantic_validate.h"
#include "sight_flow_validate.h"
#include <math.h>

/* Only these explicitly analysis-only native modes preserve the input
 * coordinates. Unknown filters and treatments are barriers, never guessed
 * transparent from their dimensions. */
static inline bool hs_sight_analysis_only(obs_source_t *source)
{
    const char *id=obs_source_get_id(source);
    const char *key=NULL;
    int expected=0;
    if(!strcmp(id,"sight_u2net_filter")){key="matte_view";expected=4;}
    else if(!strcmp(id,"sight_spatial_motion_filter")){key="flow_view";expected=0;}
    else if(!strcmp(id,"sight_text_obfuscation_filter")){key="ocr_view";expected=3;}
    else if(!strcmp(id,"sight_depth_filter")){key="view_mode";expected=2;}
    else if(!strcmp(id,"sight_privacy_v2_filter")){
        obs_data_t *cfg=obs_source_get_settings(source);
        if(!cfg)return false;
        bool analysis=obs_data_get_bool(cfg,"semantic_enabled")&&
            obs_data_get_bool(cfg,"semantic_analysis_only")&&!obs_data_get_bool(cfg,"semantic_buffered");
        obs_data_release(cfg);return analysis;
    }
    else return false;
    obs_data_t *settings=obs_source_get_settings(source);
    if(!settings)return false;
    const bool valid=obs_data_get_int(settings,key)==expected;
    obs_data_release(settings);
    return valid;
}
struct hs_sight_matte_search {
    unsigned count;
    const char *id;
};
static inline void hs_sight_matte_find(obs_source_t *parent, obs_source_t *child, void *data)
{
    (void)parent;
    struct hs_sight_matte_search *search=(struct hs_sight_matte_search *)data;
    if(!strcmp(obs_source_get_id(child),search->id)&&obs_source_enabled(child))++search->count;
}
static inline obs_source_t *hs_sight_local_producer(obs_source_t *consumer,const char *id)
{
    obs_source_t *parent=obs_filter_get_parent(consumer);
    if(!parent)return NULL;
    struct hs_sight_matte_search search={0};
    search.id=id;
    obs_source_enum_filters(parent,hs_sight_matte_find,&search);
    if(search.count!=1)return NULL;
    // Walk actual render order, not list display order. Hold a reference
    // as we walk; reject cross-source edges, unknown transforms and loops.
    obs_source_t *node=obs_source_get_ref(obs_filter_get_target(consumer));
    for(unsigned depth=0;node&&depth<16;++depth){
        if(obs_filter_get_parent(node)!=parent)break;
        if(obs_source_enabled(node)){
            if(!hs_sight_analysis_only(node))break;
            if(!strcmp(obs_source_get_id(node),id))return node;
        }
        obs_source_t *next=obs_source_get_ref(obs_filter_get_target(node));
        obs_source_release(node);node=next;
    }
    if(node)obs_source_release(node);
    return NULL;
}

static inline obs_source_t *hs_sight_subject_producer(obs_source_t *consumer)
{
    return hs_sight_local_producer(consumer,"sight_u2net_filter");
}

static inline bool hs_sight_depth_read(obs_source_t *consumer,uint64_t known,sight_depth_v1 *out)
{
    obs_source_t *producer=hs_sight_local_producer(consumer,"sight_depth_filter");
    if(!producer)return false;
    out->abi_version=SIGHT_DEPTH_ABI_V1;out->struct_size=sizeof(*out);out->sequence=0;
    calldata_t params;calldata_init(&params);
    calldata_set_int(&params,"max_age_ms",1500);
    calldata_set_int(&params,"known_sequence",(long long)known);
    calldata_set_ptr(&params,"snapshot",out);
    bool available=false;
    proc_handler_t *handler=obs_source_get_proc_handler(producer);
    if(handler&&proc_handler_call(handler,"sight_depth_read_v1",&params))
        calldata_get_bool(&params,"available",&available);
    calldata_free(&params);
    const uint32_t width=obs_source_get_base_width(producer),height=obs_source_get_base_height(producer);
    obs_source_release(producer);
    return available&&out->abi_version==SIGHT_DEPTH_ABI_V1&&out->struct_size==sizeof(*out)&&
        out->sequence&&width&&height&&out->source_width==width&&out->source_height==height&&
        out->width&&out->height&&out->width<=SIGHT_DEPTH_MAX_EDGE&&out->height<=SIGHT_DEPTH_MAX_EDGE;
}

static inline bool hs_sight_flow_read_linked(obs_source_t *consumer,sight_flow_v1 *out,
                                             uint64_t *predecessor,bool *linked)
{
    if(predecessor)*predecessor=0;
    if(linked)*linked=false;
    obs_source_t *producer=hs_sight_local_producer(consumer,"sight_spatial_motion_filter");
    if(!producer)return false;
    out->abi_version=SIGHT_FLOW_ABI_V1;out->struct_size=sizeof(*out);out->sequence=0;
    calldata_t params;calldata_init(&params);
    calldata_set_int(&params,"max_age_ms",500);
    calldata_set_ptr(&params,"snapshot",out);
    bool available=false;
    proc_handler_t *handler=obs_source_get_proc_handler(producer);
    if(handler){
        bool called=false;
        if(linked){
            called=proc_handler_call(handler,"sight_flow_linked_v1",&params);
            long long previous=0;
            if(called&&calldata_get_int(&params,"previous_sequence",&previous)&&previous>=0){
                *linked=true;
                if(predecessor)*predecessor=(uint64_t)previous;
            }
        }
        if(!called)called=proc_handler_call(handler,"sight_flow_read_v1",&params);
        if(called)calldata_get_bool(&params,"available",&available);
    }
    calldata_free(&params);
    const uint32_t width=obs_source_get_base_width(producer),height=obs_source_get_base_height(producer);
    obs_source_release(producer);
    return available&&hs_sight_flow_valid(out,width,height);
}
static inline bool hs_sight_flow_read(obs_source_t *consumer,sight_flow_v1 *out)
{
    return hs_sight_flow_read_linked(consumer,out,NULL,NULL);
}

static inline bool hs_sight_text_read(obs_source_t *consumer,sight_text_v1 *out)
{
    obs_source_t *producer=hs_sight_local_producer(consumer,"sight_text_obfuscation_filter");
    if(!producer)return false;
    out->abi_version=SIGHT_TEXT_ABI_V1;
    out->struct_size=sizeof(*out);
    out->sequence=0;out->count=0;
    calldata_t params;calldata_init(&params);
    calldata_set_int(&params,"max_age_ms",2000);
    calldata_set_ptr(&params,"snapshot",out);
    bool available=false;
    proc_handler_t *handler=obs_source_get_proc_handler(producer);
    if(handler&&proc_handler_call(handler,"sight_text_read_v1",&params))
        calldata_get_bool(&params,"available",&available);
    calldata_free(&params);
    const uint32_t width=obs_source_get_base_width(producer),height=obs_source_get_base_height(producer);
    obs_source_release(producer);
    return available&&hs_text_valid(out,width,height);
}

static inline bool hs_sight_semantic_read(obs_source_t *consumer,sight_semantic_v1 *out)
{
    memset(out,0,sizeof(*out));
    out->abi_version=SIGHT_SEMANTIC_ABI_V1;out->struct_size=sizeof(*out);
    obs_source_t *producer=hs_sight_local_producer(consumer,"sight_privacy_v2_filter");
    if(!producer)return false;
    calldata_t params;calldata_init(&params);
    calldata_set_int(&params,"max_age_ms",650);
    calldata_set_ptr(&params,"snapshot",out);
    bool available=false;
    proc_handler_t *handler=obs_source_get_proc_handler(producer);
    if(handler&&proc_handler_call(handler,"sight_semantic_geometry_v1",&params))
        calldata_get_bool(&params,"available",&available);
    calldata_free(&params);
    const uint32_t w=obs_source_get_base_width(producer),h=obs_source_get_base_height(producer);
    obs_source_release(producer);
    return available&&hs_semantic_valid(out,w,h);
}

static inline bool hs_sight_matte_read(obs_source_t *consumer, uint64_t known,
                                      sight_matte_v1 *out)
{
    obs_source_t *producer=hs_sight_subject_producer(consumer);
    if (!producer) return false;
    bool available = false;
    {
        out->abi_version = SIGHT_MATTE_ABI_V1;
        out->struct_size = sizeof(*out);
        out->sequence = 0;
        calldata_t params;
        calldata_init(&params);
        calldata_set_int(&params,"max_age_ms",750);
        calldata_set_int(&params,"known_sequence",(long long)known);
        calldata_set_ptr(&params,"snapshot",out);
        proc_handler_t *handler = obs_source_get_proc_handler(producer);
        if (handler && proc_handler_call(handler,"sight_matte_read_v1",&params))
            calldata_get_bool(&params,"available",&available);
        calldata_free(&params);
    }
    const uint32_t width=obs_source_get_base_width(producer), height=obs_source_get_base_height(producer);
    obs_source_release(producer);
    return available && out->abi_version == SIGHT_MATTE_ABI_V1 &&
        out->struct_size == sizeof(*out) && out->sequence &&
        out->width && out->height && out->width <= SIGHT_MATTE_MAX_EDGE &&
        out->height <= SIGHT_MATTE_MAX_EDGE &&
        out->source_width == width && out->source_height == height;
}

static inline bool hs_sight_motion_read(obs_source_t *consumer, sight_motion_v1 *out)
{
    obs_source_t *producer=hs_sight_subject_producer(consumer);
    if (!producer) return false;
    out->abi_version=SIGHT_MOTION_ABI_V1;
    out->struct_size=sizeof(*out);
    out->sequence=0; out->energy=0.f;
    bool available=false;
    calldata_t params;
    calldata_init(&params);
    calldata_set_int(&params,"max_age_ms",750);
    calldata_set_ptr(&params,"snapshot",out);
    proc_handler_t *handler=obs_source_get_proc_handler(producer);
    if (handler && proc_handler_call(handler,"sight_motion_read_v1",&params))
        calldata_get_bool(&params,"available",&available);
    calldata_free(&params);
    const uint32_t width=obs_source_get_base_width(producer), height=obs_source_get_base_height(producer);
    obs_source_release(producer);
    return available && out->abi_version==SIGHT_MOTION_ABI_V1 &&
        out->struct_size==sizeof(*out) && out->sequence && width && height &&
        out->source_width==width && out->source_height==height &&
        isfinite(out->energy) && out->energy>=0.f && out->energy<=1.f;
}
