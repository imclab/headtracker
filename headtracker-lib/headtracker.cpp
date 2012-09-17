#include "stdafx.h"

using namespace std;
using namespace cv;

HT_API(bool) ht_cycle(headtracker_t* ctx, ht_result_t* euler) {
	float rotation_matrix[9];
	float translation_vector[3];
	float rotation_matrix2[9];
	float translation_vector2[3];

	memset(rotation_matrix, 0, sizeof(float) * 9);
	memset(translation_vector, 0, sizeof(float) * 3);

	euler->filled = false;

	if (!ht_get_image(*ctx))
		return false;
		
	switch (ctx->state) {
	case HT_STATE_INITIALIZING: {
		if (!(ctx->focal_length > 0)) {
            double ar = (ctx->grayscale.rows / (double) ctx->grayscale.cols);
            ctx->focal_length_w = ctx->grayscale.cols / tan(0.5 * ctx->config.field_of_view * HT_PI / 180.0);
            ctx->focal_length_h = ctx->grayscale.rows / tan(0.5 * ctx->config.field_of_view * ar * HT_PI / 180.0);
            ctx->focal_length = ctx->focal_length_w;
		}
		ht_track_features(*ctx);
        if (ht_initial_guess(*ctx, ctx->grayscale, rotation_matrix, translation_vector))
		{
			ht_project_model(*ctx, rotation_matrix, translation_vector, ctx->model, cvPoint3D32f(0, 0, 0));
			ht_get_features(*ctx, ctx->model);
            double best_error;
			if (ht_ransac_best_indices(*ctx, &best_error) &&
                ctx->feature_count >= ctx->config.ransac_min_features * 2)
			{
				ctx->state = HT_STATE_TRACKING;
                ctx->restarted = false;
			} else {
				if (++ctx->init_retries > ctx->config.max_init_retries)
					ctx->state = HT_STATE_LOST;
			}
		}
		break;
	} case HT_STATE_TRACKING: {
		ht_track_features(*ctx);
        double best_error = 1.0e10;
		CvPoint3D32f offset;
		CvPoint2D32f centroid;

        if (ht_ransac_best_indices(*ctx, &best_error) &&
            ht_estimate_pose(*ctx, rotation_matrix, translation_vector, rotation_matrix2, translation_vector2, &offset, &centroid))
        {
			ht_remove_lumps(*ctx);
			ht_project_model(*ctx, rotation_matrix, translation_vector, ctx->model, cvPoint3D32f(offset.x, offset.y, offset.z));
			ht_draw_model(*ctx, ctx->model);
			ht_draw_features(*ctx);
			ht_get_features(*ctx, ctx->model);
			*euler = ht_matrix_to_euler(rotation_matrix2, translation_vector2);
			euler->filled = true;
            euler->confidence = -best_error;
			if (ctx->config.debug)
				printf("corners %d/%d (%d) keypoints %d/%d (%d)\n",
					   ctx->feature_count, ctx->config.max_tracked_features, ctx->config.feature_quality_level,
					   ctx->keypoint_count, ctx->config.max_keypoints, ctx->config.keypoint_quality);
        } else {
            if (ctx->abortp)
                abort();
			ctx->state = HT_STATE_LOST;
        }
		break;
	} case HT_STATE_LOST: {
		ctx->feature_count = 0;
		for (int i = 0; i < ctx->model.count; i++)
			ctx->features[i].x = -1;
		ctx->state = HT_STATE_INITIALIZING;
		ctx->init_retries = 0;
		ctx->restarted = true;
		ctx->depth_frame_count = 0;
		ctx->depth_counter_pos = 0;
		ctx->zoom_ratio = 1.0f;
		ctx->keypoint_count = 0;
        ctx->abortp = false;
		for (int i = 0; i < ctx->config.max_keypoints; i++)
			ctx->keypoints[i].idx = -1;
		break;
	}
	default:
		return false;
	}

    ctx->grayscale.copyTo(ctx->last_image);

	return true;
}
