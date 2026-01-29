#include <gst/gst.h>

// For GIR generation, not used in C/C++ code

typedef struct _GstAnalyticsRelationMeta {
    GstMeta parent_meta;

    /*< Next id > */
    guint next_id;

    /* Adjacency-matrix */
    guint8 **adj_mat;

    /* Lookup (offset relative to analysis_results) of relatable metadata */
    gsize *mtd_data_lookup;

    /* Relation order */
    gsize rel_order;

    /* Increment used when relation order grow */
    gsize rel_order_increment;

    /* Analysis metadata based on GstAnalyticsRelatableMtdData */
    gint8 *analysis_results;

    /* Current writing location in analysis_results buffer */
    gsize offset;

    /* Size of analysis_results buffer */
    gsize max_size;

    /* Increment of analysis_results */
    gsize max_size_increment;

    /* Number of analytic metadata (GstAnalyticsRelatableMtdData) in
     * analysis_results */
    gsize length;

} GstAnalyticsRelationMeta;

struct _GstAnalyticsMtd {
    guint id;
    GstAnalyticsRelationMeta *meta;
};
