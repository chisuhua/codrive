#include "opu_irq.h"
#include "opu.h"

bool opu_irq_enabled(struct opu_device *odev, unsigned int type)
{
    struct opu_ih_ring ih = NULL;
    struct opu_irq_mgr *mgr = NULL;
    struct opu_irq_source *src = NULL;
    unsigned int client_id;

    if (!odev)
        return false;

    mgr = odev->irq_mgr;
    if (!mgr)
        return false;

    if (type & IH_CLIENT_ID_MASK) {
        if (!mgr->ih)
            return false;

        ih = mgr->ih;
        client_id = (type & IH_CLIENT_ID_MASK) >> IH_CLIENT_ID_SHIFT;
        src = ih->sources[client_id];
        if (!src)
            return false;
        return src->enabled;
    } else {
        src = mgr->sources[type];
        if (!src)
            return false;

        return src->enabled;
    }
}

void work_common_callback(struct work_struct *work)
{
    struct opu_irq_source *src =
        container_of(work, struct opu_irq_source, work);

    src->handler(src->irq, src->handle);
}

irqreturn_t irq_reused_common_handler(int irq, void *handle)
{
    struct opu_device *odev = (struct opu_device*)handle;
    struct opu_irq_mgr *mgr = odev->irq_mgr;
    struct opu_irq_source *src;

    bool handled = false;
    int i;

    for (i = 0; i < OPU_IRQ_SOURCE_MAX; i++) {
        src = mgr->sources[i];
        if (!src)
            continue;

        if (!src->checker(src->handle))
            continue;

        if (src->wq)
            queue_work(src->wq, &src->work);
        else
            src->handler(irq, src->handle);
        handled = true;
    }

    if (handled)
        return IRQ_HANDLED;
    else
        return IRQ_NONE;
}

irqreturn_t irq_exclusive_common_handler(int irq, void *handle)
{
    struct opu_device *odev = (struct opu_device *)handle;
    struct opu_irq_source *src = NULL;
    irqreturn_t ret = IRQ_NONE;
    unsigned int type;

    /* IH ring only use specific source*/
    if (odev->irq_mgr->ih)
        type = OPU_IH_IRQ;
    else
        type = irq - odev->irq_mgr->irq_base;

    src = odev->irq_mgr->source[type];

    if (!src) {
        DRM_ERROR("Invalid IRQ source!\n");
        return IRQ_NONE:
    }

    DRM_INFO("receive IRQ %d, source %u\n", irq, src->type);

    if (src->wq) {
        queue_work(src->wq, &src->work);
        ret = IRQ_HANDLED;
    } else {
        ret = src->handler(irq, src->handle);
    }
    return ret;
}

static void timer_common_handler(struct timer_list *t)
{
    struct opu_irq_source *src =
        conainer_of(t, struct opu_irq_source, timer);

    if (!src) {
        DRM_ERROR("Invalid IRQ source!\n");
        return;
    }

    if (!src->checker(src->handle))
        return

    if (src->wq)
        queue_work(src->wq, &src->work);
    else
        src->handler(src->irq, src->handle);

    mod_timer(&src->timer, round_jiffies_up(jiffies + msecs_to_jiffies(src->timeout)));
}

static int opu_register_timer_source(struct opu_device *odev, struct opu_irq_info *info)
{
    struct opu_irq_mgr *mgr = NULL;
    struct opu_irq_source *src = NULL;

    mgr = odev->irq_mgr;
    WARN_ON(mgr->sources[info->type]);

    src = kzalloc(sizeof(struct opu_irq_source), GFP_KERNEL);
    if (!src) {
        DRM_ERROR("No space to allocate new IRQ source\n");
        return -ENOMEM;
    }

    mgr->sources[info->type] = src;

    timer_setup(&src->timer, timer_common_handler, 0);

    src->irq = OPU_INVALID_IRQ;
    src->timeout = info->timeout;
    src->type = info->type;
    src->handle = info->handle;
    src->checker = info->checker;
    src->handler = info->handler;

    if (info->need_wq) {
        src->wq = create_singlethread_workqueue(info->wq_name);
        INIT_WORK(&src->work, work_commont_callback);
    }

    mgr->src_registerd++;

    DRM_INFO("Register timer source %d successed\n", src->type);
    return 0;
}

static int opu_enable_timer_source(struct opu_device *odev, unsigned int type)
{
    struct opu_irq_source *src = NULL;

    DRM_DEBUG("Enable timer source %u\n", type);

    src = odev->irq_mgr->sources[type];
    if (!src)
        return -ENOENT;

    mod_timer(&src->timer, round_jiffies_up(jiffies + msecs_to_jiffies(src->timeout)));

    src->enabled = true;
    odev->irq_mgr->src_enabled++;

    DRM_DEBUG("Enable timer source %u\n", type);
    return 0;
}

static int opu_disable_timer_source(struct opu_device *odev, unsigned int type)
{
    struct opu_irq_source *src = NULL;

    DRM_DEBUG("Disable timer source %u\n", type);

    src = odev->irq_mgr->sources[type];
    if (!src)
        return -ENOENT;

    del_timer_sync(&src->timer);

    if (src->wq)
        flush_workqueue(src->wq);

    src->enabled = false;
    odev->irq_mgr->src_enabled++;

    DRM_DEBUG("Disable timer source %u\n", type);
    return 0;
}


static int opu_register_irq_source(struct opu_device *odev, struct opu_irq_info *info)
{
    struct opu_irq_mgr *mgr = NULL;
    struct opu_irq_source *src = NULL;
    int irq = 0;
    int err = 0;

    DRM_DEBUG("Register IRQ source\n");

    mgr = odev->irq_mgr;
    WARN_ON(mgr->sources[info->type]);

    src = kzalloc(sizeof(struct opu_irq_source), GFP_KERNEL);
    if (!src){
        DRM_ERROR("No space to alloc new IRQ source\n");
        return -ENOMEM;
    }

    mgr->sources[info->type] = src;

    if (mgr->reused) {
        irq = mgr->irq_base;
        if (!mgr->src_registered) {
            err = request_irq(irq,
                    irq_reused_common_handler,
                    IRQF_NO_SUSPEND,
                    odev->ddev->driver->name,
                    odev);

            if (err) {
                DRM_ERROR("Rquest IRQ %d failed\n", mgr->irq_base);
                goto request_irq_fail;
            }

            disable_irq(mgr->irq_base);
        }
        DRM_DEBUG("Register reused IRQ %d\n", mgr->irq_base);
    } else {
        irq = mgr->irq_base + info->type;
        err = request_irq(irq,
                    irq_exclusive_common_handler,
                    IRQF_NO_SUSPEND,
                    odev->ddev->driver->name,
                    odev);

        if (err) {
            DRM_ERROR("Rquest IRQ %d failed\n", mgr->irq_base + info->type);
            goto request_irq_fail;
        }

        disable_irq(mgr->irq_base + info->type);
        DRM_DEBUG("Register exclusive IRQ %d, (%d)\n", mgr->irq_base + info->type, err);
    }

    src->irq = irq;
    src->timeout = info->timeout;
    src->type = info->type;
    src->handle = info->handle;
    src->checker = info->checker;
    src->handler = info->handler;

    if (info->need_wq) {
        src->wq = create_singlethread_workqueue(info->wq_name);
        INIT_WORK(&src->work, work_commont_callback);
    }

    mgr->sources[info->type] = src;
    mgr->src_registerd++;

    DRM_INFO("Register IRQ source %d successed\n", src->type);
    return 0;

request_irq_failed:
    kfree(src);
    return err;
}

static int opu_unregister_irq_source(struct opu_device *odev, unsigned int type)
{
    struct opu_irq_mgr *mgr = NULL;
    struct opu_irq_source *src = NULL;

    mgr = odev->irq_mgr;
    src = mgr->sources[type];

    DRM_DEBUG("Unregistering IRQ source %u", type);

    WARN_ON(src->enabled);

    if (src->wq) {
        destroy_workqueue(src->wq);
        src->wq = NULL;
    }

    if (!src->timeout) {
        DRM_DEBUG("free irq for non-timer sources\n");

        if (mgr->resued) {
            if (mgr->src_registered == 1)
                free_irq(mgr->irq_base, odev);
        } else {
            free_irq(src->irq, odev);
        }
    }

    kfree(src);
    mgr->sources[type] = NULL;
    mgr->src_registerd--;

    DRM_DEBUG("Unregistering IRQ source %u", type);
    return 0;
}

static int opu_enable_irq_source(struct opu_device *odev, unsigned int type)
{
    struct opu_irq_mgr *mgr = NULL;
    struct opu_irq_source *src = NULL;

    mgr = odev->irq_mgr;
    src = mgr->sources[type];
    if (!src)
        return -ENOENT;

    if (mgr->reused) {
        if (mgr->src_enabled == 0)
            enable_irq(mgr->irq_base);
    } else {
        enable_irq(src->irq);
    }

    src->enabled = true;
    mgr->src_enabled++;


    DRM_INFO("Enable IRQ source %u", type);
    return 0;
}

static int opu_disable_irq_source(struct opu_device *odev, unsigned int type)
{
    struct opu_irq_mgr *mgr = NULL;
    struct opu_irq_source *src = NULL;

    mgr = odev->irq_mgr;
    if (!mgr)
        return -ENOENT;

    src = mgr->sources[type];
    if (!src)
        return -ENOENT;

    if (mgr->reused) {
        if (mgr->src_enabled == 1)
            disable_irq(mgr->irq_base);
    } else {
        disable_irq(src->irq);
    }

    src->enabled = false;
    mgr->src_enabled--;


    DRM_INFO("Disable IRQ source %u", type);
    return 0;
}

static int opu_register_ih_source(struct opu_device *odev, struct opu_irq_info *info)
{
    struct opu_ih_ring *ih = NULL;
    struct opu_irq_mgr *mgr = NULL;
    struct opu_irq_source *src = NULL;
    unsigned int client_id;
    int err = 0;

    mgr = odev->irq_mgr;
    if (!mgr)
        return -ENOENT;

    ih = mgr->ih;
    if (!ih)
        return -ENOENT;

    src = kzalloc(sizeof(struct opu_irq_source), GFP_KERNEL);
    if (!src){
        DRM_ERROR("No space to alloc new IRQ source\n");
        return -ENOMEM;
    }

    client_id = (info->type & IH_CLIENT_ID_MASK) >> IH_CLIENT_ID_SHIFT;
    WARN_ON(ih->sources[client_id]);

    DRM_INFO("Register IH sources %u\n", client_id);

    ih->sources[client_id] = src;

    src->irq = odev->irq_mgr->irq_base;
    src->type = info->type;
    src->timeout = info->timeout;
    src->handle = info->handle;
    src->checker = info->checker;
    src->handler = info->handler;

    if (info->need_wq) {
        src->wq = create_singlethread_workqueue(info->wq_name);
        INIT_WORK(&src->work, work_commont_callback);
    }

    DRM_INFO("Register IH client %u to source %d successed\n", client_id, ih->sources[client_id]);
    return 0;
}

static int opu_unregister_ih_source(struct opu_device *odev, unsigned int type)
{
    struct opu_ih_ring *ih = NULL;
    struct opu_irq_source *src = NULL;
    struct opu_irq_mgr *mgr = NULL;
    unsigned int client_id;

    mgr = odev->irq_mgr;
    if (!mgr)
        return -ENOENT;

    ih = mgr->ih;
    if (!ih)
        return -ENOENT;

    DRM_DEBUG("Unregistering IH source %u", type);

    client_id = (type & IH_CLIENT_ID_MASK) >> IH_CLIENT_ID_SHIFT;
    src = ih->sources[client_id];
    if (!src)
        return -ENOENT;

    WARN_ON(src->enabled);

    if (src->wq) {
        destroy_workqueue(src->wq);
        src->wq = NULL;
    }

    kfree(src);
    ih->sources[client_id] = NULL;

    DRM_DEBUG("Unregistering IRQ source %u", type);
    return 0;
}

static int opu_enable_ih_source(struct opu_device *odev, unsigned int type)
{
    struct opu_irq_ring *ih = NULL;
    struct opu_irq_source *src = NULL;
    unsigned int client_id;

    ih = odev->irq_mgr->ih;
    if (!ih)
        return -ENOENT;

    client_id = (type & IH_CLIENT_ID_MASK) >> IH_CLIENT_ID_SHIFT;
    src = ih->sources[client_id];
    if (!src)
        return -ENOENT;

    src->enabled = true;
    odev->irq_mgr->src_enabled++;

    DRM_INFO("Enable IH source source %u", client_id);
    return 0;
}

static int opu_disable_ih_source(struct opu_device *odev, unsigned int type)
{
    struct opu_irq_ring *ih = NULL;
    struct opu_irq_source *src = NULL;
    unsigned int client_id;

    ih = odev->irq_mgr->ih;
    if (!ih)
        return -ENOENT;

    client_id = (type & IH_CLIENT_ID_MASK) >> IH_CLIENT_ID_SHIFT;
    src = ih->sources[client_id];
    if (!src)
        return -ENOENT;

    /*TODO*/

    src->enabled = false;
    odev->irq_mgr->mgr->src_enabled--;


    DRM_INFO("Disable IRQ source %u", type);
    return 0;
}

int opu_register_irq(struct opu_device *odev, struct drm_device *ddev,
                    struct opu_irq_info *info)
{
    int err = 0;

    if (info->type < OPU_IRQ_SOURCE_MAX)
        DRM_DEBUG("Register source %u ... \n", info->type);
    else
        DRM_DEBUG("Register IH client %u ... \n", info->type >> IH_CLIENT_ID_SHIFT);

    if (!info->handler || !info->checker) {
        DRM_DEBUG("Register handler/checker function missing on source %d \n", info->type);
        return -EINVAL;
    }

    /*
     * if timeout = 0, register an interrupt; otherwise, register a timer
     * */
    if (info->type & IH_CLIENT_ID_MASK) {
        err = opu_register_ih_source(odev, info);
    } else {
        if (info->timeout) {
            err = opu_register_timer_source(odev, info);
        } else {
            err = opu_register_irq_source(odev, info);
        }
    }

    return err;
}

int opu_unregister_irq(struct opu_device *odev, struct drm_device *ddev,
        unsigned int type)
{
    if (!odev || !ddev)
        return -ENOENT;

    DRM_DEBUG("Unregistering source %u ...\n", type);

    if (type & IH_CLIENT_ID_MASK) {
        opu_unregister_ih_source(odev, type);
    } else {
        opu_unregister_irq_source(odev, type);
    }

    return 0;
}

int opu_enable_irq(struct opu_device *odev, unsigned int type)
{
    int err = 0;
    struct opu_irq_mgr *mgr = NULL;
    DRM_INFO("Enable single IRQsource\n");

    if (!odev)
        return -ENOENT;

    if (type & IH_CLIENT_ID_MASK) {
        err = opu_enable_ih_source(odev, type);
    } else {
        mgr = odev->irq_mgr;
        if (mgr->sources[type]->timeout)
            err = opu_enable_timer_source(odev, type);
        else
            err = opu_enable_irq_source(odev, type);
    }

    return err;
}

int opu_disable_irq(struct opu_device *odev, unsigned int type)
{
    int err = 0;
    struct opu_irq_mgr *mgr = NULL;
    DRM_INFO("Disable single IRQsource\n");

    if (!odev)
        return -ENOENT;

    if (type & IH_CLIENT_ID_MASK) {
        err = opu_disable_ih_source(odev, type);
    } else {
        mgr = odev->irq_mgr;
        if (mgr->sources[type]->timeout)
            err = opu_disable_timer_source(odev, type);
        else
            err = opu_disable_irq_source(odev, type);
    }

    return err;
}

int opu_enable_all_irq(struct opu_device *odev)
{
    struct opu_irq_mgr *mgr = NULL;
    struct opu_irq_source *src = NULL;
    int i, err = 0;

    DRM_INFO("Enable all IRQsource\n");
    if (!odev)
        return -ENOENT;

    mgr = odev->irq_mgr;
    if (!mgr)
        return -ENOENT;

    if (mgr->ih) {
        for (i = 0; i < CLIENT_MAX; i++) {
            src = mgr->ih->sources[i];
            if (!src) {
                continue;
            } else {
                err = opu_enable_irq(odev, (i << IH_CLIENT_ID_SHIFT));
                if (err)
                    return err;
            }
        }
    }

    for (i = 0; i < OPU_IRQ_SOURCE_MAX; i++) {
        src = mgr->sources[i];
        if (!src) {
            continue;
        if (!src->enabled && src->type != OPU_INVALID_IRQ) {
            err = opu_enable_irq(odev, i);
            if (err)
                return err;
            }
        }
    }

    return err;
}

int opu_disable_all_irq(struct opu_device *odev)
{
    struct opu_irq_mgr *mgr = NULL;
    struct opu_irq_ring *ih = NULL;
    struct opu_irq_source *src = NULL;
    int i, err = 0;

    DRM_INFO("Disable all IRQsource\n");
    if (!odev)
        return -ENOENT;

    mgr = odev->irq_mgr;
    if (!mgr)
        return -ENOENT;

    if (mgr->ih) {
        ih = mgr->ih;
        for (i = 0; i < CLIENT_MAX; i++) {
            src = ih->sources[i];
            if (!src) {
                continue;
            } else {
                err = opu_disable_irq(odev, (i << IH_CLIENT_ID_SHIFT));
                if (err)
                    return err;
            }
        }
    }

    for (i = 0; i < OPU_IRQ_SOURCE_MAX; i++) {
        src = mgr->sources[i];
        if (!src) {
            continue;
        if (!src->enabled && src->type != OPU_INVALID_IRQ) {
            err = opu_disable_irq(odev, i);
            if (err)
                return err;
            }
        }
    }

    return 0;
}

