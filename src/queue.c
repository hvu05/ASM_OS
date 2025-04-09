#include <stdio.h>
#include <stdlib.h>
#include "queue.h"

int empty(struct queue_t *q)
{
        if (q == NULL)
                return 1;
        return (q->size == 0);
}

void enqueue(struct queue_t *q, struct pcb_t *proc)
{
        /* TODO: put a new process to queue [q] */
        // hình như là push proc vào q là xong, vì mỗi queue_t q là một con trỏ kiểu proc
        // trong queue q thì có nhiều proc (một mảng), push proc(para) vào ô cuối cùng trong mảng
        // if (q == NULL || proc == NULL) return;
        if (q->size >= MAX_QUEUE_SIZE) return;
        if (proc == NULL) return;
        if (q == NULL) return;
        q->proc[q->size++] = proc;
}

struct pcb_t *dequeue(struct queue_t *q)
{
        /* TODO: return a pcb whose prioprity is the highest
         * in the queue [q] and remember to remove it from q
         * */
        // Do khi enqueue vào lộn xộn, nên khi lấy ra cần kiểm tra prio nào ưu tiên hơn sẽ lấy ra trước
        if (empty(q)) return NULL;

        struct pcb_t *proc = q->proc[0];
        for (int i = 0; i < q->size - 1; i++)
        {
                q->proc[i] = q->proc[i + 1];
        }
        q->proc[q->size - 1] = NULL;
        q->size--;
        return proc;
}
