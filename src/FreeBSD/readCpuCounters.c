/* This software is distributed under the following license:
 * http://host-sflow.sourceforge.net/license.html
 */

#if defined(__cplusplus)
extern "C" {
#endif

#include "hsflowd.h"
#include <sys/sysctl.h>
#include <sys/user.h>
#include <sys/vmmeter.h>
#include <kvm.h>


  /*_________________---------------------------__________________
    _________________   getRunningProcesses     __________________
    -----------------___________________________------------------
    or is this running_threads? $$$
  */

  static  uint32_t  getRunningProcesses( void )
  {
    struct kinfo_proc *kp;
    int i;
    int state;
    int nentries;
    kvm_t *kd = NULL;
    int what = KERN_PROC_ALL;
  
    uint32_t val32;
  
    val32 = 0;
  
    // $$$ call kvm_open() here, and kvm_close() below?
  
    if (kd) {
    
#ifdef KERN_PROC_NOTHREADS
      what |= KERN_PROC_NOTHREADS;
#endif
      
      if ((kp = kvm_getprocs(kd, what, 0, &nentries)) == 0 || nentries < 0) {
	// got nothing
      }
      else {
	
	for (i = 0; i < nentries; kp++, i++) {
#ifdef KINFO_PROC_SIZE
	  state = kp->ki_stat;
#else
	  state = kp->kp_proc.p_stat;
#endif
	  switch(state) {
	  case SRUN:
	  case SIDL:
	    val32++;
	    break;
	  }
	}
      }
    }
    
    if (val32 > 0) val32--;
    return val32;
  }
  

  /*_________________---------------------------__________________
    _________________   getTotalProcesses       __________________
    -----------------___________________________------------------
  */
  
  static uint32_t  getTotalProcesses( void )
  {
    uint32_t val32;
    struct vmtotal total;
    size_t len;
    /* computed every 5 seconds */
    len = sizeof(total);
    sysctlbyname("vm.vmtotal", &total, &len, NULL, 0);
    val32 = total.t_rq + total.t_dw + total.t_pw + total.t_sl + total.t_sw;
    return val32;
  }

  /*_________________---------------------------__________________
    _________________   getCpuSpeed             __________________
    -----------------___________________________------------------
  */

  static uint32_t  getCpuSpeed ( void )
  {
    char buf[1024];
    char *curptr;
    size_t len;
    uint32_t freq = 0, tmpfreq;
    uint64_t tscfreq;

    /*
     * If the system supports it, the cpufreq driver provides the best
     * access to CPU frequency.  Since we want a constant value, we're
     * looking for the maximum frequency, not the current one.  We
     * don't know what order the driver will report values in so we
     * search for the highest one by parsing the string returned by the
     * dev.cpu.0.freq_levels sysctl.  The format of the string is a space
     * seperated list of MHz/milliwatts.
     */
    tmpfreq = 0;
    len = sizeof(buf);
    if (sysctlbyname("dev.cpu.0.freq_levels", buf, &len, NULL, 0) == -1)
      buf[0] = '\0';
    curptr = buf;
    while (isdigit(curptr[0])) {
      freq = strtol(curptr, &curptr, 10);
      if (freq > tmpfreq)
	tmpfreq = freq;
      /* Skip the rest of this entry */
      while (!isspace(curptr[0]) && curptr[0] != '\0')
	curptr++;
      /* Find the next entry */
      while (!isdigit(curptr[0]) && curptr[0] != '\0')
	curptr++;
    }
    freq = tmpfreq;
    if (freq != 0)
      goto done;

    /*
     * machdep.tsc_freq exists on some i386/amd64 machines and gives the
     * CPU speed in Hz.  If it exists it's a decent value.
     */
    tscfreq = 0;
    len = sizeof(tscfreq);
    if (sysctlbyname("machdep.tsc_freq", &tscfreq, &len, NULL, 0) != -1) {
      freq = tscfreq / 1e6;
      goto done;
    }

  done:
    return freq;
  }


  /*_________________---------------------------__________________
    _________________     readCpuCounters       __________________
    -----------------___________________________------------------
  */
  
  int readCpuCounters(SFLHost_cpu_counters *cpu) 
  {
    int gotData = NO;
    uint64_t val64;
    size_t len;
    double load[3];
    long cp_time[CPUSTATES];

    getloadavg(load, 3);
    cpu->load_one = (float)load[0];
    cpu->load_five = (float)load[1];
    cpu->load_fifteen = (float)load[2];

    cpu->proc_run = (uint32_t)getRunningProcesses();
    cpu->proc_total = (uint32_t)getTotalProcesses();
    if (getSys64("hw.ncpu", &val64)) cpu->cpu_num = (uint32_t)val64;
    cpu->cpu_speed = (uint32_t) getCpuSpeed();

    /* puts kern.cp_time array into cp_time */
    /* constants are defined in sys/dkstat.h */
    len = sizeof(cp_time);
    if (sysctlbyname("kern.cp_time", &cp_time, &len, NULL, 0) != -1) {
      // len should be 20. is it really an array of long, though?
      // might want to just read up to 40 bytes and then see what we get.
      // myLog(LOG_INFO, "kerm.cp_time len=%u", len);
      cpu->cpu_user = (uint32_t)cp_time[CP_USER];
      cpu->cpu_nice = (uint32_t)cp_time[CP_NICE];
      cpu->cpu_system = (uint32_t)cp_time[CP_SYS];
      cpu->cpu_idle = (uint32_t)cp_time[CP_IDLE];
      cpu->cpu_intr = (uint32_t)cp_time[CP_INTR];
    }

    cpu ->cpu_wio = (uint32_t)-1; // $$$

    if(getSys64("vm.stats.sys.v_soft", &val64)) cpu->cpu_sintr = (uint32_t)val64;
    if(getSys64("vm.stats.sys.v_intr", &val64)) cpu->interrupts = (uint32_t)val64;
    if(getSys64("vm.stats.sys.v_swtch", &val64)) cpu->contexts = (uint32_t)val64;

    gotData = YES;
  
    return gotData;
  }


#if defined(__cplusplus)
} /* extern "C" */
#endif

