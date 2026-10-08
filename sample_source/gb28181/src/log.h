#ifndef LOG_H
#define LOG_H

#define LOGI(format, ...)  fprintf(stderr,"[INFO] [%s : %d ()] " format "\n", __func__ ,__LINE__,##__VA_ARGS__)
// #define LOGI(format, ...)  fprintf(stderr,"[INFO] [%s:%d %s()] " format "\n", __FILE__,__LINE__,__func__ ,##__VA_ARGS__)
#endif