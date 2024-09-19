/*
 * Copyright 2021 Xilinx Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <opencv2/core.hpp>
#include <opencv2/highgui.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/imgcodecs.hpp>
#include <iostream>
#include <sstream>
#include <math.h>
#include <ivas/ivas_kernel.h>
#include <gst/ivas/gstinferencemeta.h>
#include <chrono>


#include "ivas_airender.hpp"

// Jaosn add =============================
#include <time.h>
#include <unistd.h>
// Jaosn add =============================
// TI added for JB ======================
#include <mqueue.h>
#include <stdbool.h>
#define AIRender_VERSION "0002\n"




int log_level = LOG_LEVEL_WARNING;



using namespace cv;
using namespace std;

#define MAX_CLASS_LEN 1024
#define MAX_LABEL_LEN 1024
#define MAX_ALLOWED_CLASS 20
#define MAX_ALLOWED_LABELS 20


#define MQ_NAME "/my_queue"
#define MQ_MAXMSG 1  // Maximum number of messages
#define MAX_OBJECTS 32
#define MQ_MSGSIZE sizeof(QueueElement)  // Size of each message



typedef struct {
    unsigned short x;
    unsigned short y;
    unsigned short w;
    unsigned short h;
    unsigned char c;
    unsigned char reserve[3];
} ObjectInfo;

typedef struct {
    unsigned char person_counter;
    unsigned char lifter_counter;
    ObjectInfo objects[MAX_OBJECTS];
} QueueElement;


// Declare global variables for the message queue
struct mq_attr attr;
mqd_t mq = (mqd_t)-1;    // Message queue descriptor
QueueElement element;    // Queue element to send



// Jason add =============================
static int RESULT_last = 0;
static int STATUS_last = 0;
int TARGET_amount = 0;

const int FIFO_MAX_SIZE = 30;
const int CONF_THRES = 8;
static int res_array[FIFO_MAX_SIZE] = {0};
static int a_end = 0;
// Jason add =============================

struct color
{
  unsigned int blue;
  unsigned int green;
  unsigned int red;
};

struct ivass_xclassification
{
  color class_color;
  char class_name[MAX_CLASS_LEN];
};

struct overlayframe_info
{
  IVASFrame *inframe;
  Mat image;
  Mat I420image;
  Mat NV12image;
  Mat lumaImg;
  Mat chromaImg;
  int y_offset;
};


using Clock = std::chrono::steady_clock;

struct ivas_xoverlaypriv
{
  float font_size;
  unsigned int font;
  int line_thickness;
  int y_offset;
  color label_color;
  char label_filter[MAX_ALLOWED_LABELS][MAX_LABEL_LEN];
  unsigned char label_filter_cnt;
  unsigned short classes_count;
  ivass_xclassification class_list[MAX_ALLOWED_CLASS];
  struct overlayframe_info frameinfo;
  int drawfps;
  int fps_interv;
  double fps;
  int framecount;
  Clock::time_point startClk;
};




//Jason add
const int roi_w = 32;
const int roi_h = 18;
char ROI_map_c[roi_w*roi_h+10] ="";
int  ROI_map  [roi_w*roi_h+10] = {0};
double scl_fac_x = 0.0;
double scl_fac_y = 0.0;
int num_of_people = 0;
clock_t warn_time_last, warn_time_cur;
//Jason add


// Function to filter x0 based on time stability
int filter(int x0, int *prev_y0, int *stable_count, int stable_threshold) {
    int y0;

    // Check if x0 has remained the same
    if (x0 == *prev_y0) {
        (*stable_count)++;  // Increment stable count if x0 is unchanged
    } else {
        *stable_count = 0;  // Reset stable count if x0 changes
    }

    // Set y0 to x0 if x0 has remained unchanged for the required number of iterations
    if (*stable_count >= stable_threshold) {
        y0 = x0;
    } else {
        y0 = *prev_y0;
    }

    // Update the previous y0 and x0 for the next call
    *prev_y0 = y0;

    return y0;
}
int personcount_raw_x0 = 0;
int prev_personcount_y0 = 0;  // Initialize previous y0
int stable_personcount = 0;  // Count of how many times x0 has remained the same
int liftercount_raw_x0 = 0;
int prev_liftercount_y0 = 0;  // Initialize previous y0
int stable_liftercount = 0;  // Count of how many times x0 has remained the same
int prev_personcount_output = 0;
int prev_liftercount_output = 0;


/* Check if the given classification is to be filtered */
int
ivas_classification_is_allowed (char *cls_name, ivas_xoverlaypriv * kpriv)
{
  unsigned int idx;

  if (cls_name == NULL)
    return -1;

  for (idx = 0;
      idx < sizeof (kpriv->class_list) / sizeof (kpriv->class_list[0]); idx++) {
    if (!strcmp (cls_name, kpriv->class_list[idx].class_name)) {
      return idx;
    }
  }
  return -1;
}

/* Get y and uv color components corresponding to givne RGB color */
void
convert_rgb_to_yuv_clrs (color clr, unsigned char *y, unsigned short *uv)
{
  Mat YUVmat;
  Mat BGRmat (2, 2, CV_8UC3, Scalar (clr.red, clr.green, clr.blue));
  cvtColor (BGRmat, YUVmat, cv::COLOR_BGR2YUV_I420);
  *y = YUVmat.at < uchar > (0, 0);
  *uv = YUVmat.at < uchar > (2, 0) << 8 | YUVmat.at < uchar > (2, 1);
  return;
}

/* Compose label text based on config json */
bool
get_label_text (GstInferenceClassification * c, ivas_xoverlaypriv * kpriv,
    char *label_string)
{
  unsigned char idx = 0, buffIdx = 0;
  if (!c->class_label || !strlen ((char *) c->class_label))
    return false;

  for (idx = 0; idx < kpriv->label_filter_cnt; idx++) {
    if (!strcmp (kpriv->label_filter[idx], "class")) {
      sprintf (label_string + buffIdx, "%s", (char *) c->class_label);
      buffIdx += strlen (label_string);
    } else if (!strcmp (kpriv->label_filter[idx], "probability")) {
      sprintf (label_string + buffIdx, " : %.2f ", c->class_prob);
      buffIdx += strlen (label_string);
    }
  }
  return true;
}

int check_invaded_Jason(BoundingBox target)
{
    int a = (int)((target.x-2)/scl_fac_x + 0.5);
    int b = (int)((target.y-2)/scl_fac_y + 0.5);
    int c = (int)((target.width+4)/scl_fac_x + 0.5);
    int d = (int)((target.height+4)/scl_fac_y + 0.5);

    int i, j;

    //printf("a: %u, b: %u, c: %u, d: %u\n", target.x, target.y, target.width, target.height);
    //printf("a: %d, b: %d, c: %d, d: %d\n", a, b, c, d);

    for(i=a;i<a+c;i++)
    {
        for(j=b;b=j<b+d;j++)
        {
            if(ROI_map[j*roi_w+i] == 1)
                return 1;
        }
    } 

    return 0;
}

int push_q(int in)
{
    if(a_end < FIFO_MAX_SIZE-1)
        a_end++;
    else
        a_end = 0;

    res_array[a_end] = in;
    
    return a_end;
}

int read_q(int idx)
{
    int pos = a_end + idx;

    if(pos >= 0)
        return res_array[pos];
    else
        return res_array[FIFO_MAX_SIZE+pos];
}

int check_inference_status()
{
    printf("res_last 5: %d, %d, %d, %d, %d\n", read_q(0), read_q(-1), read_q(-2), read_q(-3), read_q(-4));
    
    return  (read_q(0)==1 || read_q(-1)==1 || read_q(-2)==1 || read_q(-3) || read_q(-4)==1);
}


void call_warn_in_shell_Jason(BoundingBox target, int res)
{
    char cmd[300];
    FILE * fp1;
    static int cnt = 0;

    double time_interval;
    static int confident = 0;
    int status = 0;
    int nullpoints = 0;
struct timespec tp1,tp2;

    //push_q(res);   
    if(res==1)confident++;
    
    warn_time_cur = clock();

    //printf(" last: %ld, cur: %ld,", warn_time_last, warn_time_cur);
    time_interval = ((double) (warn_time_cur - warn_time_last)) / CLOCKS_PER_SEC;
    //printf(" time_interval: %lf,", time_interval);

    //printf(" confident: %d,", confident);
    if(time_interval < 1.0) 
    {
        //puts(" not enough 1 sec, return !!");
        return;
    }
    //puts("");

    //if(!check_inference_status(res)) return;

    if(confident < CONF_THRES)  status = 0;
    else                        status = 1;

    if(status ^ STATUS_last)
    {
        sprintf(cmd, "/etc/rc5.d/gt_aibox/warning_cam1.sh %d %d cam1 &", status, confident);
        fp1 = popen(cmd, "w"); 
        pclose(fp1);
    }

    warn_time_last = warn_time_cur;
    confident = 0;
    STATUS_last = status;
}



static gboolean
overlay_node_foreach (GNode * node, gpointer kpriv_ptr)
{
 
  static int overlaystatus=0;
  unsigned char qbuffer[2];
  struct timespec tp1,tp2 ;	
  static struct timespec tpFrame1,tpFrame2;
  uint16_t areanum=1;
  uint16_t cleanmask=0xFFFF;
  int i;
  long t_diff,t_Framediff;
clock_gettime(CLOCK_MONOTONIC_RAW, &tp1);
  ivas_xoverlaypriv *kpriv = (ivas_xoverlaypriv *) kpriv_ptr;
  struct overlayframe_info *frameinfo = &(kpriv->frameinfo);
  LOG_MESSAGE (LOG_LEVEL_DEBUG, "enter");

    

  GList *classes;
  GstInferenceClassification *classification;
  GstInferencePrediction *prediction = (GstInferencePrediction *) node->data;
//raw number  a,b
//
//int personcount_raw_x0 = 0;
//int prev_personcount_y0 = 0;  // Initialize previous y0
//int stable_personcount = 0;  // Count of how many times x0 has remained the same
//output


  if(NULL==prediction->classifications) //NULL==prediction->classifications
  {
     printf("end  end  end\n");
    

    int personcount_output=0; 
    int liftercount_output=0; 



    personcount_output = filter(personcount_raw_x0, &prev_personcount_y0, &stable_personcount, 10);
    liftercount_output = personcount_output; //only for develomentp
    printf("output=%d, raw=%d stable=%d\n",personcount_output,personcount_raw_x0,stable_personcount);
    printf("prev_personcount_output=%d, personcount_output=%d\n",prev_personcount_output,personcount_output);
    //if personcount_ouput_condition is true
    //if liftercount_ouput_condition is true
    if((prev_personcount_output!=personcount_output)||(prev_liftercount_output!=liftercount_output))
    {

        // Populate 'element' with dummy data
        memset(&element, 0, sizeof(QueueElement));
        element.person_counter = (unsigned char)personcount_output;
        element.lifter_counter = (unsigned char)liftercount_output;

        // Set dummy object data if needed
        for (int i = 0; i < MAX_OBJECTS; i++)
        {
            element.objects[i].x = 0;
            element.objects[i].y = 0;
            element.objects[i].w = 0;
            element.objects[i].h = 0;
            element.objects[i].c = 0;
        }
        

        // Before sending, clear the queue by receiving any existing messages
        char recv_buf[MQ_MSGSIZE];
        ssize_t bytes_received;
        while ((bytes_received = mq_receive(mq, recv_buf, MQ_MSGSIZE, NULL)) >= 0) {
            // Discard received message
        }
        if (errno != EAGAIN && errno != ENOMSG) {
            perror("mq_receive failed");
            // Handle error as needed
        }

        // Send 'element' via message queue
        if (mq_send(mq, (const char *)&element, sizeof(QueueElement), 0) == -1)
        {
                perror("mq_send failed");
        }

        // Update previous counts
        prev_personcount_output = personcount_output;
        prev_liftercount_output = liftercount_output;

    }
    prev_personcount_y0=personcount_raw_x0;
    prev_liftercount_y0=liftercount_raw_x0;
    personcount_raw_x0=0;
    liftercount_raw_x0=0;

  }


   

/* On each children, iterate through the different associated classes */
  for (classes = prediction->classifications;
      classes; classes = g_list_next (classes)) {
    classification = (GstInferenceClassification *) classes->data;


    int idx = ivas_classification_is_allowed ((char *)
        classification->class_label, kpriv);
    if (kpriv->classes_count && idx == -1)
      continue;





    color clr;
    if (kpriv->classes_count) {
      clr = {
      kpriv->class_list[idx].class_color.blue,
            kpriv->class_list[idx].class_color.green,
            kpriv->class_list[idx].class_color.red};
    } else {
      /* If there are no classes specified, we will go with default blue */
      clr = {
      //255, 0, 0};
      0, 200, 128};
    }

    char label_string[MAX_LABEL_LEN];
    bool label_present;
    Size textsize;
    label_present = get_label_text (classification, kpriv, label_string);

    if (label_present) {
      int baseline;
      textsize = getTextSize (label_string, kpriv->font,
          kpriv->font_size, 1, &baseline);
      /* Get y offset to use in case of classification model */
      if ((prediction->bbox.height < 1) && (prediction->bbox.width < 1)) {
        if (kpriv->y_offset) {
          frameinfo->y_offset = kpriv->y_offset;
        } else {
          frameinfo->y_offset = (frameinfo->inframe->props.height * 0.10);
        }
      }
    }


    switch(classification->class_id)
    {
        case 0:
        {
            int idx=personcount_raw_x0+liftercount_raw_x0;

            personcount_raw_x0=personcount_raw_x0+1;
            element.objects[i].x = 0;
            element.objects[i].y = 0;
            element.objects[i].w = 0;
            element.objects[i].h = 0;
            element.objects[i].c = 0;

        }
        break;
        case 1:
        {  
            int idx=personcount_raw_x0+liftercount_raw_x0;

            liftercount_raw_x0=liftercount_raw_x0+1;
            
            element.objects[i].x = 0;
            element.objects[i].y = 0;
            element.objects[i].w = 0;
            element.objects[i].h = 0;
            element.objects[i].c = 0;
        }
        break;

    }

    printf(
        "RESULT: (prediction node %ld) %s(%d) %d %d %d %d (%f)\n",
        prediction->prediction_id,
        label_present ? classification->class_label : NULL,
        classification->class_id, prediction->bbox.x, prediction->bbox.y,
        prediction->bbox.width + prediction->bbox.x,
        prediction->bbox.height + prediction->bbox.y,
        classification->class_prob);
    LOG_MESSAGE (LOG_LEVEL_INFO,
        "RESULT: (prediction node %ld) %s(%d) %d %d %d %d (%f)",
        prediction->prediction_id,
        label_present ? classification->class_label : NULL,
        classification->class_id, prediction->bbox.x, prediction->bbox.y,
        prediction->bbox.width + prediction->bbox.x,
        prediction->bbox.height + prediction->bbox.y,
        classification->class_prob);

    
    /* Check whether the frame is NV12 or BGR and act accordingly */
    if (frameinfo->inframe->props.fmt == IVAS_VFMT_Y_UV8_420) {	    
      LOG_MESSAGE (LOG_LEVEL_DEBUG, "Drawing rectangle for NV12 image");
 
      unsigned char yScalar;
      unsigned short uvScalar;
      convert_rgb_to_yuv_clrs (clr, &yScalar, &uvScalar);
      /* Draw rectangle on y an uv plane */
      int new_xmin = floor (prediction->bbox.x / 2) * 2;
      int new_ymin = floor (prediction->bbox.y / 2) * 2;
      int new_xmax =
          floor ((prediction->bbox.width + prediction->bbox.x) / 2) * 2;
      int new_ymax =
          floor ((prediction->bbox.height + prediction->bbox.y) / 2) * 2;
      Size test_rect (new_xmax - new_xmin, new_ymax - new_ymin);




      if (!(!prediction->bbox.x && !prediction->bbox.y)) {
        rectangle (frameinfo->lumaImg, Point (new_xmin,
              new_ymin), Point (new_xmax,
              new_ymax), Scalar (yScalar), kpriv->line_thickness, 1, 0);
        rectangle (frameinfo->chromaImg, Point (new_xmin / 2,
              new_ymin / 2), Point (new_xmax / 2,
              new_ymax / 2), Scalar (uvScalar), kpriv->line_thickness, 1, 0);
      }
    
   

      if (label_present) {
        /* Draw filled rectangle for labelling, both on y and uv plane */
        rectangle (frameinfo->lumaImg, Rect (Point (new_xmin,
                    new_ymin - textsize.height), textsize),
            Scalar (yScalar), FILLED, 1, 0);
        textsize.height /= 2;
        textsize.width /= 2;
        rectangle (frameinfo->chromaImg, Rect (Point (new_xmin / 2,
                    new_ymin / 2 - textsize.height), textsize),
            Scalar (uvScalar), FILLED, 1, 0);

        /* Draw label text on the filled rectanngle */
        convert_rgb_to_yuv_clrs (kpriv->label_color, &yScalar, &uvScalar);
        putText (frameinfo->lumaImg, label_string, cv::Point (new_xmin,
                new_ymin + frameinfo->y_offset), kpriv->font, kpriv->font_size,
            Scalar (yScalar), 1, 1);
        putText (frameinfo->chromaImg, label_string, cv::Point (new_xmin / 2,
                new_ymin / 2 + frameinfo->y_offset / 2), kpriv->font,
            kpriv->font_size / 2, Scalar (uvScalar), 1, 1);
      }
     
    } else if (frameinfo->inframe->props.fmt == IVAS_VFMT_BGR8) {
      LOG_MESSAGE (LOG_LEVEL_DEBUG, "Drawing rectangle for BGR image");
      if (!(!prediction->bbox.x && !prediction->bbox.y)) {
        /* Draw rectangle over the dectected object */
        rectangle (frameinfo->image, Point (prediction->bbox.x,
              prediction->bbox.y),
          Point (prediction->bbox.width + prediction->bbox.x,
              prediction->bbox.height + prediction->bbox.y), Scalar (clr.blue,
              clr.green, clr.red), kpriv->line_thickness, 1, 0);
      }

      if (label_present) {
        /* Draw filled rectangle for label */
        rectangle (frameinfo->image, Rect (Point (prediction->bbox.x,
                    prediction->bbox.y - textsize.height), textsize),
            Scalar (clr.blue, clr.green, clr.red), FILLED, 1, 0);

        /* Draw label text on the filled rectanngle */
        putText (frameinfo->image, label_string,
            cv::Point (prediction->bbox.x,
                prediction->bbox.y + frameinfo->y_offset), kpriv->font,
            kpriv->font_size, Scalar (kpriv->label_color.blue,
                kpriv->label_color.green, kpriv->label_color.red), 1, 1);
      }
    }
  }

clock_gettime(CLOCK_MONOTONIC_RAW, &tp2) ;
t_diff = ((tp2.tv_sec-tp1.tv_sec)*(1000*1000*1000) + (tp2.tv_nsec-tp1.tv_nsec)) / 1000 ;
//printf("%lu usec passed\n",t_diff) ;

  return FALSE;
}

static void
fps_overlay(gpointer kpriv_ptr)
{
  ivas_xoverlaypriv *kpriv = (ivas_xoverlaypriv *) kpriv_ptr;
  if (!kpriv->drawfps)
  {
      return ;
  }
  struct overlayframe_info *frameinfo = &(kpriv->frameinfo);

  if (kpriv->framecount == 0)
  {
      kpriv->startClk = Clock::now();
  }
  else 
  {
      if (kpriv->framecount%kpriv->fps_interv == 0)
      {

          Clock::time_point nowClk = Clock::now();
          int duration = (std::chrono::duration_cast<std::chrono::milliseconds>(nowClk - kpriv->startClk)).count();
          kpriv->fps = kpriv->framecount * 1e3 / duration ;
      }

      color clr = {255, 0, 0};
      int new_xmin = 50;
      int new_ymin = 50;

      std::ostringstream oss;
      oss  << "Framerate:" << kpriv->fps << " FPS";
      Size textsize;

      if (frameinfo->inframe->props.fmt == IVAS_VFMT_Y_UV8_420) {
          unsigned char yScalar;
          unsigned short uvScalar;
          convert_rgb_to_yuv_clrs (clr, &yScalar, &uvScalar);
          {
              /* Draw label text on the filled rectanngle */
              convert_rgb_to_yuv_clrs (kpriv->label_color, &yScalar, &uvScalar);
              putText (frameinfo->lumaImg, oss.str(), cv::Point (new_xmin,
                          new_ymin), kpriv->font, kpriv->font_size,
                      Scalar (yScalar), 1, 1);
              putText (frameinfo->chromaImg, oss.str(), cv::Point (new_xmin / 2,
                          new_ymin / 2), kpriv->font,
                      kpriv->font_size / 2, Scalar (uvScalar), 1, 1);
          }
      } else if (frameinfo->inframe->props.fmt == IVAS_VFMT_BGR8) {
          LOG_MESSAGE (LOG_LEVEL_DEBUG, "Drawing rectangle for BGR image");
          {
              /* Draw label text on the filled rectanngle */
              putText (frameinfo->image, oss.str(),
                      cv::Point (new_xmin, new_ymin), kpriv->font,
                      kpriv->font_size, Scalar (clr.blue,
                          clr.green, clr.red), 1, 1);
          }
      }
  }
  kpriv->framecount++;

  return ;
}

extern "C"
{
  int32_t xlnx_kernel_init (IVASKernel * handle)
  {
//Jason add
  /*  int i;
    FILE* roimap_f = fopen("/etc/rc5.d/gt_aibox/roi_map.txt", "r");
    fread(ROI_map_c, roi_w*roi_h, 1, roimap_f);
    for(i=0;i<roi_w*roi_h;i++)
    {
        ROI_map[i] = (int)((ROI_map_c[i]) - '0');
        //printf("%d, ", ROI_map[i]);
        //if(i%9 ==0 && i != 0)
        //    puts("");
    }
    scl_fac_x = 1920.0/(double)roi_w;
    scl_fac_y = 1080.0/(double)roi_h;
    printf("scl_fac_x: %lf, scl_fac_y: %lf\n", scl_fac_x, scl_fac_y);
    */
//Jason add



    LOG_MESSAGE (LOG_LEVEL_DEBUG, "enter");

    ivas_xoverlaypriv *kpriv =
        (ivas_xoverlaypriv *) calloc (1, sizeof (ivas_xoverlaypriv));

    json_t *jconfig = handle->kernel_config;
    json_t *val, *karray = NULL, *classes = NULL;

    /* Initialize config params with default values */
    log_level = LOG_LEVEL_WARNING;
    kpriv->font_size = 0.5;
    kpriv->font = 0;
    kpriv->line_thickness = 1;
    kpriv->y_offset = 0;
    kpriv->label_color = {0, 0, 0};
    strcpy(kpriv->label_filter[0], "class");
    strcpy(kpriv->label_filter[1], "probability");
    kpriv->label_filter_cnt = 2;
    kpriv->classes_count = 0;
    kpriv->framecount = 0;

    char* env = getenv("SMARTCAM_SCREENFPS");
    if (env)
    {
        kpriv->drawfps = 1;
    }
    else
    {
        kpriv->drawfps = 0;
    }

    val = json_object_get (jconfig, "fps_interval");
    if (!val || !json_is_integer (val))
        kpriv->fps_interv = 1;
    else
        kpriv->fps_interv = json_integer_value (val);

    val = json_object_get (jconfig, "debug_level");
    if (!val || !json_is_integer (val))
        log_level = LOG_LEVEL_WARNING;
    else
        log_level = json_integer_value (val);

      val = json_object_get (jconfig, "font_size");
    if (!val || !json_is_integer (val))
        kpriv->font_size = 0.5;
    else
        kpriv->font_size = json_integer_value (val);

      val = json_object_get (jconfig, "font");
    if (!val || !json_is_integer (val))
        kpriv->font = 0;
    else
        kpriv->font = json_integer_value (val);

      val = json_object_get (jconfig, "thickness");
    if (!val || !json_is_integer (val))
        kpriv->line_thickness = 1;
    else
        kpriv->line_thickness = json_integer_value (val);

      val = json_object_get (jconfig, "y_offset");
    if (!val || !json_is_integer (val))
        kpriv->y_offset = 0;
    else
        kpriv->y_offset = json_integer_value (val);

    /* get label color array */
      karray = json_object_get (jconfig, "label_color");
    if (!karray)
    {
      LOG_MESSAGE (LOG_LEVEL_ERROR, "failed to find label_color");
      return -1;
    } else
    {
      kpriv->label_color.blue =
          json_integer_value (json_object_get (karray, "blue"));
      kpriv->label_color.green =
          json_integer_value (json_object_get (karray, "green"));
      kpriv->label_color.red =
          json_integer_value (json_object_get (karray, "red"));
    }

    karray = json_object_get (jconfig, "label_filter");

    if (!json_is_array (karray)) {
      LOG_MESSAGE (LOG_LEVEL_ERROR, "label_filter not found in the config\n");
      return -1;
    }
    kpriv->label_filter_cnt = 0;
    for (unsigned int index = 0; index < json_array_size (karray); index++) {
      strcpy (kpriv->label_filter[index],
          json_string_value (json_array_get (karray, index)));
      kpriv->label_filter_cnt++;
    }

    /* get classes array */
    karray = json_object_get (jconfig, "classes");
    if (!karray) {
      LOG_MESSAGE (LOG_LEVEL_ERROR, "failed to find key labels");
      return -1;
    }

    if (!json_is_array (karray)) {
      LOG_MESSAGE (LOG_LEVEL_ERROR, "labels key is not of array type");
      return -1;
    }
    kpriv->classes_count = json_array_size (karray);
    for (unsigned int index = 0; index < kpriv->classes_count; index++) {
      classes = json_array_get (karray, index);
      if (!classes) {
        LOG_MESSAGE (LOG_LEVEL_ERROR, "failed to get class object");
        return -1;
      }

      val = json_object_get (classes, "name");
      if (!json_is_string (val)) {
        LOG_MESSAGE (LOG_LEVEL_ERROR, "name is not found for array %d", index);
        return -1;
      } else {
        strncpy (kpriv->class_list[index].class_name,
            (char *) json_string_value (val), MAX_CLASS_LEN - 1);
        LOG_MESSAGE (LOG_LEVEL_DEBUG, "name %s",
            kpriv->class_list[index].class_name);
      }

      val = json_object_get (classes, "green");
      if (!val || !json_is_integer (val))
        kpriv->class_list[index].class_color.green = 0;
      else
        kpriv->class_list[index].class_color.green = json_integer_value (val);

      val = json_object_get (classes, "blue");
      if (!val || !json_is_integer (val))
        kpriv->class_list[index].class_color.blue = 0;
      else
        kpriv->class_list[index].class_color.blue = json_integer_value (val);

      val = json_object_get (classes, "red");
      if (!val || !json_is_integer (val))
        kpriv->class_list[index].class_color.red = 0;
      else
        kpriv->class_list[index].class_color.red = json_integer_value (val);
    }

    handle->kernel_priv = (void *) kpriv;
 

    printf(AIRender_VERSION);
    
    // Initialize message queue attributes
    attr.mq_flags = 0; // Blocking mode
    attr.mq_maxmsg = MQ_MAXMSG;
    attr.mq_msgsize = MQ_MSGSIZE;
    attr.mq_curmsgs = 0;

    mq = mq_open(MQ_NAME, O_WRONLY | O_CREAT|O_NONBLOCK, 0644, &attr);
    if (mq == (mqd_t) -1) {
        perror("mq_open failed");
        exit(EXIT_FAILURE);
    }
    
    return 0;
  }

  uint32_t xlnx_kernel_deinit (IVASKernel * handle)
  {
    LOG_MESSAGE (LOG_LEVEL_DEBUG, "enter");
    ivas_xoverlaypriv *kpriv = (ivas_xoverlaypriv *) handle->kernel_priv;

    if (kpriv)
      free (kpriv);

    return 0;
  }


  uint32_t xlnx_kernel_start (IVASKernel * handle, int start,
      IVASFrame * input[MAX_NUM_OBJECT], IVASFrame * output[MAX_NUM_OBJECT])
  {

    LOG_MESSAGE (LOG_LEVEL_DEBUG, "enter");

    GstInferenceMeta *infer_meta = NULL;
    char *pstr;

    ivas_xoverlaypriv *kpriv = (ivas_xoverlaypriv *) handle->kernel_priv;
    struct overlayframe_info *frameinfo = &(kpriv->frameinfo);
    frameinfo->y_offset = 0;
    frameinfo->inframe = input[0];
    char *indata = (char *) frameinfo->inframe->vaddr[0];
    char *lumaBuf = (char *) frameinfo->inframe->vaddr[0];
    char *chromaBuf = (char *) frameinfo->inframe->vaddr[1];
    infer_meta = ((GstInferenceMeta *) gst_buffer_get_meta ((GstBuffer *)
            frameinfo->inframe->app_priv, gst_inference_meta_api_get_type ()));
    if (infer_meta == NULL) {
      LOG_MESSAGE (LOG_LEVEL_ERROR,
          "ivas meta data is not available for postdpu");
      return false;
    } else {
      LOG_MESSAGE (LOG_LEVEL_DEBUG, "ivas_mata ptr %p", infer_meta);
    }

    if (frameinfo->inframe->props.fmt == IVAS_VFMT_Y_UV8_420) {
      LOG_MESSAGE (LOG_LEVEL_DEBUG, "Input frame is in NV12 format\n");
      frameinfo->lumaImg.create (input[0]->props.height, input[0]->props.stride,
          CV_8UC1);
      frameinfo->lumaImg.data = (unsigned char *) lumaBuf;
      frameinfo->chromaImg.create (input[0]->props.height / 2,
          input[0]->props.stride / 2, CV_16UC1);
      frameinfo->chromaImg.data = (unsigned char *) chromaBuf;
    } else if (frameinfo->inframe->props.fmt == IVAS_VFMT_BGR8) {
      LOG_MESSAGE (LOG_LEVEL_DEBUG, "Input frame is in BGR format\n");
      frameinfo->image.create (input[0]->props.height,
          input[0]->props.stride / 3, CV_8UC3);
      frameinfo->image.data = (unsigned char *) indata;
    } else {
      LOG_MESSAGE (LOG_LEVEL_WARNING, "Unsupported color format\n");
      return 0;
    }


    /* Print the entire prediction tree */
    pstr = gst_inference_prediction_to_string (infer_meta->prediction);
    LOG_MESSAGE (LOG_LEVEL_DEBUG, "Prediction tree: \n%s", pstr);




// Jason add

//    double a = infer_meta->prediction->bbox.x;
//    double b = infer_meta->prediction->bbox.y;
//    double c = infer_meta->prediction->bbox.width;
//    double d = infer_meta->prediction->bbox.height;

//    printf("%.0lf %.0lf %.0lf %.0lf\n", a, b, c, d);

    //GSList *subpreds = NULL;
    //GSList *iter = NULL;
    
    //subpreds = prediction_get_children_unlocked (infer_meta->prediction);

    //for (iter = subpreds; iter != NULL; iter = g_slist_next (iter)) {
    //    GstInferencePrediction *pred = (GstInferencePrediction *) iter->data;
    //    gchar *child = prediction_to_string (pred, level + 1);

    //    g_string_append_printf (string, "%s, ", child);
    //    g_free (child);
    //}

    //printf("%s\n", pstr);

//    if(!g_node_get_root(infer_meta->prediction->predictions))
//        puts("tree is nul");
//    puts("end if Json add");
    TARGET_amount = 0;
    int node_num = g_node_n_nodes(infer_meta->prediction->predictions, G_TRAVERSE_ALL);
    TARGET_amount = node_num;
    //printf("node num: %d\n", node_num);
    BoundingBox dummy;

    static int my_cnt = 0;
    //printf("Jason test %d ========================= (%d) \n", my_cnt++, node_num);

    call_warn_in_shell_Jason(dummy, -1);

// Jason add




    free (pstr);

    g_node_traverse (infer_meta->prediction->predictions, G_PRE_ORDER,
        G_TRAVERSE_ALL, -1, overlay_node_foreach, kpriv);

    fps_overlay(kpriv);
    return 0;
  }


  int32_t xlnx_kernel_done (IVASKernel * handle)
  {
    LOG_MESSAGE (LOG_LEVEL_DEBUG, "enter");
    return 0;
  }
}





