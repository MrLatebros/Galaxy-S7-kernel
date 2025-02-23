/*
 * Author: Paul Reioux aka Faux123 <reioux@gmail.com>
 *
 * Copyright 2012~2014 Paul Reioux
 * Copyright 2017 DeadneM
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */
#include <linux/workqueue.h>
#include <linux/cpu.h>
#include <linux/sched.h>
#include <linux/mutex.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/input.h>
#include <linux/cpufreq.h>

#ifdef CONFIG_POWERSUSPEND
#include <linux/powersuspend.h>
#endif

//#define DEBUG_INTELLI_PLUG
#undef DEBUG_INTELLI_PLUG
#define INTELLI_PLUG_MAJOR_VERSION	4
#define INTELLI_PLUG_MINOR_VERSION	4

#define DEF_SAMPLING_MS			300
#define DUAL_PERSISTENCE		(2500 / DEF_SAMPLING_MS)
#define TRI_PERSISTENCE			(1700 / DEF_SAMPLING_MS)
#define QUAD_PERSISTENCE		(1000 / DEF_SAMPLING_MS)
#define BUSY_PERSISTENCE		(3500 / DEF_SAMPLING_MS)
#define CAPACITY_RESERVE		50
#define THREAD_CAPACITY 		(430 - CAPACITY_RESERVE)
#define MULT_FACTOR			4
#define DIV_FACTOR			100000
#define NR_FSHIFT			3
#define NR_RUN_ECO_MODE_PROFILE		3
#define NR_RUN_HYSTERESIS_QUAD		8
#define NR_RUN_HYSTERESIS_DUAL		4
#define CPU_NR_THRESHOLD		((THREAD_CAPACITY << 1) + (THREAD_CAPACITY / 2))

static DEFINE_MUTEX(intelli_plug_mutex);
static struct delayed_work intelli_plug_work;
static struct delayed_work intelli_plug_boost;
static struct workqueue_struct *intelliplug_wq;
static struct workqueue_struct *intelliplug_boost_wq;
/**/
static unsigned int intelli_plug_active = 0;
module_param(intelli_plug_active, uint, 0664);

static unsigned int intelli_plug_wakeup = 0;
module_param(intelli_plug_wakeup, uint, 0664);

static unsigned int touch_boost_active = 1;
module_param(touch_boost_active, uint, 0664);

static unsigned int nr_run_profile_sel = 0;
module_param(nr_run_profile_sel, uint, 0664);

static unsigned int screen_off_max = 2;
module_param(screen_off_max, uint, 0664);

static unsigned int sampling_time = DEF_SAMPLING_MS;
module_param(sampling_time, uint, 0664);

static unsigned int cpu_nr_run_threshold = CPU_NR_THRESHOLD;
module_param(cpu_nr_run_threshold, uint, 0664);

static unsigned int nr_run_hysteresis = NR_RUN_HYSTERESIS_QUAD;
module_param(nr_run_hysteresis, uint, 0664);
/**/
static unsigned int nr_possible_cores;
static int persist_count = 0;
static bool suspended = false;
static unsigned int nr_fshift = NR_FSHIFT;

struct ip_cpu_info {
	unsigned int sys_max;
	unsigned int cur_max;
	unsigned long cpu_nr_running;
};

static DEFINE_PER_CPU(struct ip_cpu_info, ip_info);

static unsigned int nr_run_thresholds_balance[] = {
	(THREAD_CAPACITY * 625 * MULT_FACTOR) / DIV_FACTOR,
	(THREAD_CAPACITY * 875 * MULT_FACTOR) / DIV_FACTOR,
	(THREAD_CAPACITY * 1125 * MULT_FACTOR) / DIV_FACTOR,
	UINT_MAX
};

static unsigned int nr_run_thresholds_performance[] = {
	(THREAD_CAPACITY * 380 * MULT_FACTOR) / DIV_FACTOR,
	(THREAD_CAPACITY * 625 * MULT_FACTOR) / DIV_FACTOR,
	(THREAD_CAPACITY * 875 * MULT_FACTOR) / DIV_FACTOR,
	UINT_MAX
};

static unsigned int nr_run_thresholds_conservative[] = {
	(THREAD_CAPACITY * 875 * MULT_FACTOR) / DIV_FACTOR,
	(THREAD_CAPACITY * 1625 * MULT_FACTOR) / DIV_FACTOR,
	(THREAD_CAPACITY * 2125 * MULT_FACTOR) / DIV_FACTOR,
	UINT_MAX
};

static unsigned int nr_run_thresholds_eco[] = {
        (THREAD_CAPACITY * 380 * MULT_FACTOR) / DIV_FACTOR,
	UINT_MAX
};

static unsigned int nr_run_thresholds_eco_extreme[] = {
        (THREAD_CAPACITY * 750 * MULT_FACTOR) / DIV_FACTOR,
	UINT_MAX
};

static unsigned int nr_run_thresholds_disable[] = {
	0,  0,  0,  UINT_MAX
};

static unsigned int *nr_run_profiles[] = {
	nr_run_thresholds_balance,
	nr_run_thresholds_performance,
	nr_run_thresholds_conservative,
	nr_run_thresholds_eco,
	nr_run_thresholds_eco_extreme,
	nr_run_thresholds_disable,
};

static unsigned int nr_run_last;
// extern unsigned long avg_nr_running(void);
// extern unsigned long avg_cpu_nr_running(unsigned int cpu);
// added
static inline unsigned int do_avg_nr_running(struct rq *rq)
{

	struct nr_stats_s *nr_stats = &per_cpu(runqueue_stats, rq->cpu);
	unsigned int ave_nr_running = nr_stats->ave_nr_running;
	s64 nr, deltax;

	deltax = rq->clock_task - nr_stats->nr_last_stamp;
	nr = NR_AVE_SCALE(rq->nr_running);

	if (deltax > NR_AVE_PERIOD)
		ave_nr_running = nr;
	else
		ave_nr_running +=
			NR_AVE_DIV_PERIOD(deltax * (nr - ave_nr_running));

	return ave_nr_running;
}

unsigned long avg_nr_running(void)
{
	unsigned long i, sum = 0;
	unsigned int seqcnt, ave_nr_running;

	for_each_online_cpu(i) {
		struct nr_stats_s *stats = &per_cpu(runqueue_stats, i);
		struct rq *q = cpu_rq(i);

		/*
		 * Update average to avoid reading stalled value if there were
		 * no run-queue changes for a long time. On the other hand if
		 * the changes are happening right now, just read current value
		 * directly.
		 */
		seqcnt = read_seqcount_begin(&stats->ave_seqcnt);
		ave_nr_running = do_avg_nr_running(q);
		if (read_seqcount_retry(&stats->ave_seqcnt, seqcnt)) {
			read_seqcount_begin(&stats->ave_seqcnt);
			ave_nr_running = stats->ave_nr_running;
		}

		sum += ave_nr_running;
	}

	return sum;
}

unsigned long avg_cpu_nr_running(unsigned int cpu)
{
	unsigned int seqcnt, ave_nr_running;

	struct nr_stats_s *stats = &per_cpu(runqueue_stats, cpu);
	struct rq *q = cpu_rq(cpu);

	/*
	 * Update average to avoid reading stalled value if there were
	 * no run-queue changes for a long time. On the other hand if
	 * the changes are happening right now, just read current value
	 * directly.
	 */
	seqcnt = read_seqcount_begin(&stats->ave_seqcnt);
	ave_nr_running = do_avg_nr_running(q);
	if (read_seqcount_retry(&stats->ave_seqcnt, seqcnt)) {
		read_seqcount_begin(&stats->ave_seqcnt);
		ave_nr_running = stats->ave_nr_running;
	}

	return ave_nr_running;
}
// end

static unsigned int calculate_thread_stats(void)
{
	unsigned int avg_nr_run = avg_nr_running();
	unsigned int nr_run;
	unsigned int threshold_size;
	unsigned int *current_profile;

	current_profile = nr_run_profiles[nr_run_profile_sel];
	if (num_possible_cpus() > 2) {
		if (nr_run_profile_sel >= NR_RUN_ECO_MODE_PROFILE)
			threshold_size =
				ARRAY_SIZE(nr_run_thresholds_eco);
		else
			threshold_size =
				ARRAY_SIZE(nr_run_thresholds_balance);
	} else
		threshold_size =
			ARRAY_SIZE(nr_run_thresholds_eco);

	if (nr_run_profile_sel >= NR_RUN_ECO_MODE_PROFILE)
		nr_fshift = 1;
	else
		nr_fshift = num_possible_cpus() - 1;

	for (nr_run = 1; nr_run < threshold_size; nr_run++) {
		unsigned int nr_threshold;
		nr_threshold = current_profile[nr_run - 1];

		if (nr_run_last <= nr_run)
			nr_threshold += nr_run_hysteresis;
		if (avg_nr_run <= (nr_threshold << (FSHIFT - nr_fshift)))
			break;
	}
	nr_run_last = nr_run;

	return nr_run;
}

static void __ref intelli_plug_boost_fn(struct work_struct *work)
{

	int nr_cpus = num_online_cpus();

	if (intelli_plug_active)
		if (touch_boost_active)
			if (nr_cpus < 2)
				cpu_up(1);
}

/*
static int cmp_nr_running(const void *a, const void *b)
{
	return *(unsigned long *)a - *(unsigned long *)b;
}
*/

static void update_per_cpu_stat(void)
{
	unsigned int cpu;
	struct ip_cpu_info *l_ip_info;

	for_each_online_cpu(cpu) {
		l_ip_info = &per_cpu(ip_info, cpu);
		l_ip_info->cpu_nr_running = avg_cpu_nr_running(cpu);
#ifdef DEBUG_INTELLI_PLUG
		pr_info("cpu %u nr_running => %lu\n", cpu,
			l_ip_info->cpu_nr_running);
#endif
	}
}

static void unplug_cpu(int min_active_cpu)
{
	unsigned int cpu;
	struct ip_cpu_info *l_ip_info;
	int l_nr_threshold;

	for_each_online_cpu(cpu) {
		l_nr_threshold =
			cpu_nr_run_threshold << 1 / (num_online_cpus());
		if (cpu == 0)
			continue;
		l_ip_info = &per_cpu(ip_info, cpu);
		if (cpu > min_active_cpu)
			if (l_ip_info->cpu_nr_running < l_nr_threshold)
				cpu_down(cpu);
	}
}

static void __ref intelli_plug_work_fn(struct work_struct *work)
{
	unsigned int nr_run_stat;
	unsigned int cpu_count = 0;
	unsigned int nr_cpus = 0;

	int i;

	if (intelli_plug_active) {
		nr_run_stat = calculate_thread_stats();
		update_per_cpu_stat();
#ifdef DEBUG_INTELLI_PLUG
		pr_info("nr_run_stat: %u\n", nr_run_stat);
#endif
		cpu_count = nr_run_stat;
		nr_cpus = num_online_cpus();

		if (!suspended) {

			if (persist_count > 0)
				persist_count--;

			switch (cpu_count) {
			case 1:
				if (persist_count == 0) {
					//take down everyone
					unplug_cpu(0);
				}
#ifdef DEBUG_INTELLI_PLUG
				pr_info("case 1: %u\n", persist_count);
#endif
				break;
			case 2:
				if (persist_count == 0)
					persist_count = DUAL_PERSISTENCE;
				if (nr_cpus < 2) {
					for (i = 1; i < cpu_count; i++)
						cpu_up(i);
				} else {
					unplug_cpu(1);
				}
#ifdef DEBUG_INTELLI_PLUG
				pr_info("case 2: %u\n", persist_count);
#endif
				break;
			case 3:
				if (persist_count == 0)
					persist_count = TRI_PERSISTENCE;
				if (nr_cpus < 3) {
					for (i = 1; i < cpu_count; i++)
						cpu_up(i);
				} else {
					unplug_cpu(2);
				}
#ifdef DEBUG_INTELLI_PLUG
				pr_info("case 3: %u\n", persist_count);
#endif
				break;
			case 4:
				if (persist_count == 0)
					persist_count = QUAD_PERSISTENCE;
				if (nr_cpus < 4)
					for (i = 1; i < cpu_count; i++)
						cpu_up(i);
#ifdef DEBUG_INTELLI_PLUG
				pr_info("case 4: %u\n", persist_count);
#endif
				break;
			default:
				pr_err("Run Stat Error: Bad value %u\n", nr_run_stat);
				break;
			}
		}
#ifdef DEBUG_INTELLI_PLUG
		else
			pr_info("intelli_plug is suspened!\n");
#endif
	}
	queue_delayed_work_on(0, intelliplug_wq, &intelli_plug_work,
		msecs_to_jiffies(sampling_time));
}

#ifdef CONFIG_POWERSUSPEND
static void screen_off_limit(bool on)
{
	unsigned int cpu;
	struct cpufreq_policy *policy;
	struct ip_cpu_info *l_ip_info;

	/* not active, so exit */
	if (screen_off_max == UINT_MAX)
		return;

	for_each_online_cpu(cpu) {
		l_ip_info = &per_cpu(ip_info, cpu);
		policy = cpufreq_cpu_get(0);

		if (on) {
			/* save current instance */
			l_ip_info->cur_max = policy->max;
			policy->max = screen_off_max;
			policy->cpuinfo.max_freq = screen_off_max;
#ifdef DEBUG_INTELLI_PLUG
			pr_info("cpuinfo max is (on): %u %u\n",
				policy->cpuinfo.max_freq, l_ip_info->sys_max);
#endif
		} else {
			/* restore */
			if (cpu != 0) {
				l_ip_info = &per_cpu(ip_info, 0);
			}
			policy->cpuinfo.max_freq = l_ip_info->sys_max;
			policy->max = l_ip_info->cur_max;
#ifdef DEBUG_INTELLI_PLUG
			pr_info("cpuinfo max is (off): %u %u\n",
				policy->cpuinfo.max_freq, l_ip_info->sys_max);
#endif
		}
		cpufreq_update_policy(cpu);
	}
}

void __ref intelli_plug_perf_boost(bool on)
{
	unsigned int cpu;

	if (intelli_plug_active) {
		flush_workqueue(intelliplug_wq);
		if (on) {
			for_each_possible_cpu(cpu) {
				if (!cpu_online(cpu))
					cpu_up(cpu);
			}
		} else {
			queue_delayed_work_on(0, intelliplug_wq,
				&intelli_plug_work,
				msecs_to_jiffies(sampling_time));
		}
	}
}

/* sysfs interface for performance boost (BEGIN) */
static ssize_t intelli_plug_perf_boost_store(struct kobject *kobj,
			struct kobj_attribute *attr, const char *buf,
			size_t count)
{

	int boost_req;

	sscanf(buf, "%du", &boost_req);

	switch(boost_req) {
		case 0:
			intelli_plug_perf_boost(0);
			return count;
		case 1:
			intelli_plug_perf_boost(1);
			return count;
		default:
			return -EINVAL;
	}
}

static struct kobj_attribute intelli_plug_perf_boost_attribute =
	__ATTR(perf_boost, 0664,
		NULL,
		intelli_plug_perf_boost_store);

static struct attribute *intelli_plug_perf_boost_attrs[] = {
	&intelli_plug_perf_boost_attribute.attr,
	NULL,
};

static struct attribute_group intelli_plug_perf_boost_attr_group = {
	.attrs = intelli_plug_perf_boost_attrs,
};

static struct kobject *intelli_plug_perf_boost_kobj;
/* sysfs interface for performance boost (END) */

#ifdef CONFIG_POWERSUSPEND
static void intelli_plug_suspend(struct power_suspend *handler)
#endif
{
	if (intelli_plug_active) {
		int cpu;
	
		flush_workqueue(intelliplug_wq);

		mutex_lock(&intelli_plug_mutex);
		suspended = true;
		screen_off_limit(true);
		mutex_unlock(&intelli_plug_mutex);

		// put rest of the cores to sleep unconditionally!
		for_each_online_cpu(cpu) {
			if (cpu != 0)
				cpu_down(cpu);
		}
	}
}

static void intelli_wakeup(void)
{
	unsigned int cpu;
	struct cpufreq_policy *policy;
	struct ip_cpu_info *l_ip_info;
	for_each_online_cpu(cpu) {
		policy = cpufreq_cpu_get(0);
		l_ip_info = &per_cpu(ip_info, 0);
		policy->cur = l_ip_info->cur_max;
		cpufreq_update_policy(cpu);
	}
}

static void __ref intelli_plug_resume(struct power_suspend *handler)
{

	if (intelli_plug_active) {
		int cpu;

		mutex_lock(&intelli_plug_mutex);
		/* keep cores awake long enough for faster wake up */
		persist_count = BUSY_PERSISTENCE;
		suspended = false;
		mutex_unlock(&intelli_plug_mutex);

		for_each_possible_cpu(cpu) {
			if (cpu == 0)
				continue;
			cpu_up(cpu);
		}
		intelli_wakeup();
		screen_off_limit(false);
	}
	queue_delayed_work_on(0, intelliplug_wq, &intelli_plug_work,
		msecs_to_jiffies(10));
}

static struct power_suspend intelli_plug_power_suspend_driver = {
	.suspend = intelli_plug_suspend,
	.resume = intelli_plug_resume,
};
#endif  /* CONFIG_POWERSUSPEND */

static void intelli_plug_input_event(struct input_handle *handle,
		unsigned int type, unsigned int code, int value)
{
#ifdef DEBUG_INTELLI_PLUG
	pr_info("intelli_plug touched!\n");
#endif
	queue_delayed_work_on(0, intelliplug_wq, &intelli_plug_boost,
		msecs_to_jiffies(10));
}

static int intelli_plug_input_connect(struct input_handler *handler,
		struct input_dev *dev, const struct input_device_id *id)
{
	struct input_handle *handle;
	int error;

	handle = kzalloc(sizeof(struct input_handle), GFP_KERNEL);
	if (!handle)
		return -ENOMEM;

	handle->dev = dev;
	handle->handler = handler;
	handle->name = "intelliplug";

	error = input_register_handle(handle);
	if (error)
		goto err2;

	error = input_open_device(handle);
	if (error)
		goto err1;
	pr_info("%s found and connected!\n", dev->name);
	return 0;
err1:
	input_unregister_handle(handle);
err2:
	kfree(handle);
	return error;
}

static void intelli_plug_input_disconnect(struct input_handle *handle)
{
	input_close_device(handle);
	input_unregister_handle(handle);
	kfree(handle);
}

static const struct input_device_id intelli_plug_ids[] = {
	{
		.flags = INPUT_DEVICE_ID_MATCH_EVBIT |
			 INPUT_DEVICE_ID_MATCH_ABSBIT,
		.evbit = { BIT_MASK(EV_ABS) },
		.absbit = { [BIT_WORD(ABS_MT_POSITION_X)] =
			    BIT_MASK(ABS_MT_POSITION_X) |
			    BIT_MASK(ABS_MT_POSITION_Y) },
	}, /* multi-touch touchscreen */
	{
		.flags = INPUT_DEVICE_ID_MATCH_KEYBIT |
			 INPUT_DEVICE_ID_MATCH_ABSBIT,
		.keybit = { [BIT_WORD(BTN_TOUCH)] = BIT_MASK(BTN_TOUCH) },
		.absbit = { [BIT_WORD(ABS_X)] =
			    BIT_MASK(ABS_X) | BIT_MASK(ABS_Y) },
	}, /* touchpad */
	{ },
};

static struct input_handler intelli_plug_input_handler = {
	.event          = intelli_plug_input_event,
	.connect        = intelli_plug_input_connect,
	.disconnect     = intelli_plug_input_disconnect,
	.name           = "intelliplug_handler",
	.id_table       = intelli_plug_ids,
};

int __init intelli_plug_init(void)
{
	int rc;
#ifdef CONFIG_POWERSUSPEND
	struct cpufreq_policy *policy;
	struct ip_cpu_info *l_ip_info;
#endif

	nr_possible_cores = num_possible_cpus();

	pr_info("intelli_plug: version %d.%d by faux123\n",
		 INTELLI_PLUG_MAJOR_VERSION,
		 INTELLI_PLUG_MINOR_VERSION);

	if (nr_possible_cores > 2) {
		nr_run_hysteresis = NR_RUN_HYSTERESIS_QUAD;
		nr_run_profile_sel = 0;
	} else {
		nr_run_hysteresis = NR_RUN_HYSTERESIS_DUAL;
		nr_run_profile_sel = NR_RUN_ECO_MODE_PROFILE;
	}

#ifdef CONFIG_POWERSUSPEND
	l_ip_info = &per_cpu(ip_info, 0);
	policy = cpufreq_cpu_get(0);
	l_ip_info->sys_max = policy->cpuinfo.max_freq;
	l_ip_info->cur_max = policy->max;
#endif

	rc = input_register_handler(&intelli_plug_input_handler);
#ifdef CONFIG_POWERSUSPEND
	register_power_suspend(&intelli_plug_power_suspend_driver);
#endif
	intelliplug_wq = alloc_workqueue("intelliplug",
				WQ_HIGHPRI | WQ_UNBOUND, 1);
	intelliplug_boost_wq = alloc_workqueue("iplug_boost",
				WQ_HIGHPRI | WQ_UNBOUND, 1);
	INIT_DELAYED_WORK(&intelli_plug_work, intelli_plug_work_fn);
	INIT_DELAYED_WORK(&intelli_plug_boost, intelli_plug_boost_fn);
	queue_delayed_work_on(0, intelliplug_wq, &intelli_plug_work,
		msecs_to_jiffies(10));

	intelli_plug_perf_boost_kobj
		= kobject_create_and_add("intelli_plug", kernel_kobj);
	if (intelli_plug_perf_boost_kobj) {
		rc = sysfs_create_group(intelli_plug_perf_boost_kobj,
				&intelli_plug_perf_boost_attr_group);
		if (rc)
			kobject_put(intelli_plug_perf_boost_kobj);
	}
	return 0;
}

MODULE_AUTHOR("Paul Reioux <reioux@gmail.com>");
MODULE_DESCRIPTION("'intell_plug' - An intelligent cpu hotplug driver for "
	"Low Latency Frequency Transition capable processors");
MODULE_LICENSE("GPL");

late_initcall(intelli_plug_init);

