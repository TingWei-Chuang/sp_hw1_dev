#define MAX_CLIENTS 20
#define FROM_LEN 500
#define CONTENT_LEN 2000
#define RECORD_NUM 10
#define RECORD_PATH "./BulletinBoard"

typedef struct {
 char From[FROM_LEN];
 char Content[CONTENT_LEN];
} record;
