#! /usr/bin/env python3

"""Collect the corresponding information of nic"""

import os
import shutil
import argparse
from subprocess import Popen, PIPE

DEV_LIST = ["0x4000", "0x6000", "0x3800"]
VENDOR_LIST = ["0x19ee", "0x1da8"]
CONFIG_DEBUG_SCMD = False
SCRIPT_LOG = ""
BSP_PATHS = ["/opt/corigine/bin", "/opt/netronome/bin"]
REQ_SHELL_TOOLS = ["tar"]


# Map of island number to human readable component
# isl_nr: (name, nr_me's)
ISL_MAP = {
    1: ("ARM", 4),
    4: ("PCIe0", 4),
    5: ("PCIe1", 4),
    6: ("PCIe2", 4),
    7: ("PCIe3", 4),
    8: ("NBI0", 0),
    9: ("NBI1", 0),
    12: ("Crypto0", 8),
    13: ("Crypto1", 8),
    24: ("ExternalMUEng0", 0),
    25: ("ExternalMUEng1", 0),
    26: ("ExternalMUEng2", 0),
    28: ("InternalMUEng0", 0),
    29: ("InternalMUEng1", 0),
    32: ("MECl0", 12),
    33: ("MECl1", 12),
    34: ("MECl2", 12),
    35: ("MECl3", 12),
    36: ("MECl4", 12),
    37: ("MECl5", 12),
    38: ("MECl6", 12),
    48: ("Interlaken-LA0", 4),
    49: ("Interlaken-LA1", 4),
    }


rtsyms_to_dump = [
    "BLM_0_ISLAND_ID",
    "MAP_CMSG_Q_BASE",
    "SLICC_HASH_PAD_DATA",
    "_BLM_NBI8_BLQ0_EMU_QD_BASE",
    "_BLM_NBI8_BLQ0_EMU_Q_BASE",
    "_BLM_NBI8_BLQ1_EMU_QD_BASE",
    "_BLM_NBI8_BLQ1_EMU_Q_BASE",
    "_BLM_NBI8_BLQ2_EMU_QD_BASE",
    "_BLM_NBI8_BLQ2_EMU_Q_BASE",
    "_BLM_NBI8_BLQ3_EMU_QD_BASE",
    "_BLM_NBI8_BLQ3_EMU_Q_BASE",
    "_NIC_CFG_MCAST_INSTR_TBL",
    "__pv_pkt_sequencer",
    "_abi_cfg_lut_error_cnt",
    "_abi_dcb_cfg",
    "_abi_nfd_out_red_offload_0",
    "_abi_pcie_error_cnt",
    "_cfg_error_rss_cntr",
    "_fl_buf_sz_cache",
    "_gro_bm_0_0",
    "_gro_bm_0_1",
    "_gro_bm_0_2",
    "_gro_bm_0_3",
    "_gro_bm_1_0",
    "_gro_bm_1_1",
    "_gro_bm_1_2",
    "_gro_bm_1_3",
    "_gro_bm_2_0",
    "_gro_bm_2_1",
    "_gro_bm_2_2",
    "_gro_bm_2_3",
    "_gro_bm_3_0",
    "_gro_bm_3_1",
    "_gro_bm_3_2",
    "_gro_bm_3_3",
    "_gro_cli_cntrs",
    "_gro_global_config",
    "_gro_q_0_0",
    "_gro_q_0_1",
    "_gro_q_0_2",
    "_gro_q_0_3",
    "_gro_q_1_0",
    "_gro_q_1_1",
    "_gro_q_1_2",
    "_gro_q_1_3",
    "_gro_q_2_0",
    "_gro_q_2_1",
    "_gro_q_2_2",
    "_gro_q_2_3",
    "_gro_q_3_0",
    "_gro_q_3_1",
    "_gro_q_3_2",
    "_gro_q_3_3",
    "_mac_lkup_tbl",
    "_mac_stats",
    "_multi_pf_natqs_mask",
    "_multi_pf_of_vid",
    "_multi_pf_vf_alloc",
    "_nfd_cfg_multi_pf_state",
    "_nfd_stats_in_recv",
    "_nfd_stats_out_drop",
    "_nfd_stats_out_sent",
    "_nic_cfg_synch",
    "_nic_stats_queue",
    "_nic_stats_vnic",
    "_nic_vlan_to_vnics_map_tbl",
    "_no_mcast_filter_cache",
    "_pf0_net_app_cap",
    "_pf0_net_app_id",
    "_pf0_net_bar0",
    "_pf0_net_ctrl_bar",
    "_pf0_net_vf_cfg2",
    "_pf1_net_app_cap",
    "_pf1_net_app_id",
    "_pf1_net_bar0",
    "_pf1_net_vf_cfg2",
    "blm_pci4_blq_stash",
    "cmsg_dbg_enq__cntr__",
    "cmsg_dbg_rxq__cntr__",
    "cmsg_enq__cntr__",
    "cmsg_err__cntr__",
    "cmsg_err_no_credits__cntr__",
    "cmsg_rx__cntr__",
    "cmsg_rx_bad_type__cntr__",
    "cmsg_tx__cntr__",
    "debugdebug_journal",
    "fl_cache_mem0",
    "gro_release_ring_mem_0",
    "gro_release_ring_mem_1",
    "gro_release_ring_mem_2",
    "gro_release_ring_mem_3",
    "i12.NIC_CFG_INSTR_TBL_NBI",
    "i12.NIC_CFG_INSTR_TBL_NFD",
    "i12.NIC_RSS_TBL",
    "i12._feature_counters",
    "i32.NIC_CFG_INSTR_TBL_NBI",
    "i32.NIC_CFG_INSTR_TBL_NFD",
    "i32.NIC_RSS_TBL",
    "i32._feature_counters",
    "i33.NIC_CFG_INSTR_TBL_NBI",
    "i33.NIC_CFG_INSTR_TBL_NFD",
    "i33.NIC_RSS_TBL",
    "i33._feature_counters",
    "i34.BLM_BLQ0_CACHE_BASE",
    "i34.BLM_BLQ1_CACHE_BASE",
    "i34.BLM_BLQ2_CACHE_BASE",
    "i34.BLM_BLQ3_CACHE_BASE",
    "i34.BLM_EGRESS_NULL_BUF_RECYCLE_BASE_0",
    "i34.BLM_INFO_SECTION_BASE",
    "i34.BLM_INGRESS_NULL_BUF_RECYCLE_BASE_0",
    "i34.CTM_BLQ0_STATS_BASE",
    "i34.CTM_BLQ1_STATS_BASE",
    "i34.CTM_BLQ2_STATS_BASE",
    "i34.CTM_BLQ3_STATS_BASE",
    "i34.CTM_NBI_BLQ0_STATS_BASE",
    "i34.CTM_NBI_BLQ1_STATS_BASE",
    "i34.CTM_NBI_BLQ2_STATS_BASE",
    "i34.CTM_NBI_BLQ3_STATS_BASE",
    "i34.NIC_CFG_INSTR_TBL_NBI",
    "i34.NIC_CFG_INSTR_TBL_NFD",
    "i34.NIC_RSS_TBL",
    "i34._feature_counters",
    "i34._nvnic_macs",
    "i34.me10._multicast_addr_cntrs",
    "i34.me10._queue_stat_error_flag",
    "i34.me10._vf_cfg_rate",
    "i35.NIC_CFG_INSTR_TBL_NBI",
    "i35.NIC_CFG_INSTR_TBL_NFD",
    "i35.NIC_RSS_TBL",
    "i35._feature_counters",
    "i35._pkt_buf_ctm_credits",
    "i36.NIC_CFG_INSTR_TBL_NBI",
    "i36.NIC_CFG_INSTR_TBL_NFD",
    "i36.NIC_RSS_TBL",
    "i36._feature_counters",
    "i36._pkt_buf_ctm_credits",
    "i36.me10._msix_cls_automask",
    "i36.me10._msix_cls_rx_enabled",
    "i36.me10._msix_cls_rx_entries",
    "i36.me10._msix_cls_rx_new_enabled",
    "i36.me10._msix_cls_tx_enabled",
    "i36.me10._msix_cls_tx_entries",
    "i36.me10._msix_cls_tx_new_enabled",
    "i36.me10._msix_rx_irqc_cfg",
    "i36.me10._msix_rx_irqc_state",
    "i36.me10._msix_tx_irqc_cfg",
    "i36.me10._msix_tx_irqc_state",
    "i4._lso_hdr_data",
    "i4._nfd_in_issued_ring0",
    "i4._nfd_in_issued_ring1",
    "i4.me7._msix_cls_automask",
    "i4.me7._msix_cls_rx_enabled",
    "i4.me7._msix_cls_rx_entries",
    "i4.me7._msix_cls_rx_new_enabled",
    "i4.me7._msix_cls_tx_enabled",
    "i4.me7._msix_cls_tx_entries",
    "i4.me7._msix_cls_tx_new_enabled",
    "i4.me7._msix_cls_txr_wb_enabled",
    "i4.me7._msix_rx_irqc_cfg",
    "i4.me7._msix_rx_irqc_state",
    "i4.me7._msix_tx_irqc_cfg",
    "i4.me7._msix_tx_irqc_state",
    "i4.me7._msix_txr_wb_addr",
    "i4.nfd_active_bmsk_atomic",
    "i4.nfd_in_active_bmsk",
    "i4.nfd_in_batch_mem",
    "i4.nfd_in_gpd_wq_mem0",
    "i4.nfd_out_active_bmsk",
    "i4.nfd_out_active_bmsk_atomic",
    "i4.nfd_out_sb_wq_credits0",
    "i4.nfd_out_sb_wq_mem0",
    "i4.nfd_queue_event_mem0",
    "i48.BLM_BLQ0_CACHE_BASE",
    "i48.BLM_BLQ1_CACHE_BASE",
    "i48.BLM_BLQ2_CACHE_BASE",
    "i48.BLM_BLQ3_CACHE_BASE",
    "i48.BLM_EGRESS_NULL_BUF_RECYCLE_BASE_0",
    "i48.BLM_INFO_SECTION_BASE",
    "i48.BLM_INGRESS_NULL_BUF_RECYCLE_BASE_0",
    "i48.CTM_BLQ0_STATS_BASE",
    "i48.CTM_BLQ1_STATS_BASE",
    "i48.CTM_BLQ2_STATS_BASE",
    "i48.CTM_BLQ3_STATS_BASE",
    "i48.CTM_NBI_BLQ0_STATS_BASE",
    "i48.CTM_NBI_BLQ1_STATS_BASE",
    "i48.CTM_NBI_BLQ2_STATS_BASE",
    "i48.CTM_NBI_BLQ3_STATS_BASE",
    "i48.nfd_out_sb_wq_credits0",
    "i48.nfd_out_sb_wq_mem0",
    "mem_meter",
    "mem_meter_cfg",
    "nbi0_dma_bpe_credits",
    "nfd_cfg_bar0_base0",
    "nfd_cfg_bar1_base0",
    "nfd_cfg_base0",
    "nfd_cfg_msg_jrnl",
    "nfd_cfg_pf0_num_ports",
    "nfd_cfg_pf1_num_ports",
    "nfd_cfg_ring_mem",
    "nfd_cfg_ring_mem00",
    "nfd_cfg_ring_mem01",
    "nfd_cfg_ring_mem02",
    "nfd_cfg_ring_mem03",
    "nfd_cfg_ring_mem04",
    "nfd_cfg_ring_mem05",
    "nfd_cfg_ring_mem06",
    "nfd_cfg_state_jrnl",
    "nfd_cfg_tlv_template0",
    "nfd_flr_atomic",
    "nfd_flr_seen",
    "nfd_in_cntrs0",
    "nfd_in_gather_debug_queue_state0",
    "nfd_in_gather_debug_state0",
    "nfd_in_halt_wq_mem",
    "nfd_in_pd_debug_state0",
    "nfd_in_tx_sent0",
    "nfd_init_done",
    "nfd_init_done_atomic",
    "nfd_out_atomics0",
    "nfd_out_cacher_dbg0",
    "nfd_out_cacher_qdbg0",
    "nfd_out_cntrs0",
    "nfd_out_ring_mem0",
    "nfd_out_sb_debug_state0",
    "nfd_out_sb_dma_done0",
    "nfd_out_sb_release0",
    "nfd_vf_cfg_max_vfs",
    "num_ov_alloc__cntr__",
    "num_ov_free__cntr__",
    "pkt_counters_base0",
    "pkt_counters_base1",
    "pkt_counters_base2",
    "pkt_counters_base3",
    "pkt_counters_base4",
    "pkt_counters_base5",
    "pkt_counters_base6",
    "sriov_journal_cmsg",
    "unroll_overflow_err__cntr__",
]

NbiXPB_to_dump = [
    "TrafficManager.SchedulerL1Deficit.SchedulerDeficit{0..1023}",
    "TrafficManager.SchedulerL1Weight.SchedulerWeight{0..1023}",
    "TrafficManager.SchedulerL2Deficit.SchedulerDeficit{0..1023}",
    "TrafficManager.SchedulerL2Weight.SchedulerWeight{0..1023}",

    "TrafficManager.TMQueueReg.QueueConfig{0..1023}",
    "TrafficManager.TMQueueReg.QueueDropCount{0..1023}",
    "TrafficManager.TMQueueReg.QueueStatus{0..1023}",

    "TrafficManager.TMSchedulerReg.SchedulerConfig{0..144}",
    "TrafficManager.TMSchedulerReg.SchedulerDeficit{0..127}",
    "TrafficManager.TMSchedulerReg.SchedulerWeight{0..127}",

    "TrafficManager.TMShaperReg.ShaperMaxOvershoot{0..144}",
    "TrafficManager.TMShaperReg.ShaperRate{0..144}",
    "TrafficManager.TMShaperReg.ShaperRateAdjust{0..144}",
    "TrafficManager.TMShaperReg.ShaperStatus{0..144}",
    "TrafficManager.TMShaperReg.ShaperThreshold{0..144}",

    "TrafficManager.TrafficManagerReg.BlqEvent",
    "TrafficManager.TrafficManagerReg.DropRate",
    "TrafficManager.TrafficManagerReg.EgressPullIdPortEnable",
    "TrafficManager.TrafficManagerReg.EgressRateLimit",
    "TrafficManager.TrafficManagerReg.MiniPktChannelCredit{0..127}",
    "TrafficManager.TrafficManagerReg.MiniPktCreditConfig",
    "TrafficManager.TrafficManagerReg.MiniPktFreePoolCredit0",
    "TrafficManager.TrafficManagerReg.MiniPktFreePoolCredit1",
    "TrafficManager.TrafficManagerReg.ReorderActivity",
    "TrafficManager.TrafficManagerReg.TrafficManagerConfig",

    "PktPreclassifier.Characterization.Config",
    "PktPreclassifier.Characterization.TableSet",
    "PktPreclassifier.Picoengine.PicoengineSetup",
    "PktPreclassifier.Picoengine.TableExtend",
    "PktPreclassifier.Picoengine.ActiveSet0Low",
    "PktPreclassifier.Picoengine.ActiveSet0High",
    "PktPreclassifier.Picoengine.ActiveSet1Low",
    "PktPreclassifier.Picoengine.ActiveSet1High",
    "PktPreclassifier.Picoengine.ClassifiedSmall",
    "PktPreclassifier.Picoengine.ClassifiedLarge",
    "PktPreclassifier.Picoengine.Tunnel",
    "PktPreclassifier.PolicerSequencer.Accumulator{0..7}",
    "PktPreclassifier.PolicerSequencer.Config",
    "PktPreclassifier.PolicerSequencer.Sequence{0..7}",
    ]


def parse_cmdline():
    """Use argparse to read command line variables."""
    parser = argparse.ArgumentParser(
        description='Collect various bits of debugging information')
    parser.add_argument(
        '-i',
        '--interface',
        dest='iface',
        nargs='?',
        action='store',
        default='',
        help="Optional - nfp netdev to collect information on")
    parser.add_argument(
        '-l',
        '--logdir',
        dest='logdir',
        action='store',
        default='/var/tmp',
        help="Optional - base directory to use for logs")

    args = parser.parse_args()

    # Verify arguments
    if args.iface:
        iface = args.iface
        if iface not in os.listdir("/sys/class/net"):
            print(f"{iface} not found on system")
            exit(1)
        with open(f"/sys/class/net/{iface}/device/vendor", "r") as vendor_f:
            vendor = vendor_f.read().strip()
            if vendor not in VENDOR_LIST:
                print(f"{iface} with vendor id: {vendor} is not "
                      "a valid nfp device")
                exit(1)

    return args


def get_nfps():
    """Return dict of NFP's on the system, key is the BDF."""
    # Walk /sys/bus/pci/devices - this removes the need to depend on
    # lspci being installed
    nfp_dict = {}
    devices = '/sys/bus/pci/devices'
    for root, dirs, files in os.walk(devices):
        for pci in dirs:
            with open(f"{root}/{pci}/vendor", "r") as vendor_f:
                vendor = vendor_f.read().strip()
                if vendor in VENDOR_LIST:
                    with open(f"{root}/{pci}/device", "r") as dev_f:
                        device = dev_f.read().strip()
                        if device in DEV_LIST:
                            nfp_dict[pci] = {}
    return nfp_dict


def check_bsp_tools_installed():
    """Check if bsp tools are installed on the system.
       Returns the path of the installation if found, else empty string
    """
    # Check if in path by checking "which"
    path = shutil.which("nfp-hwinfo")
    if path is not None:
        return os.path.dirname(path)

    # Otherwise check common path(s) for tools installation
    for path in BSP_PATHS:
        if os.path.exists(path) and "nfp-hwinfo" in os.listdir(path):
            return path

    return ""


def scmd(command, fail=True):
    """Run bash commands from python.

    Args:
        - command:    Command to execute
        - fail: Stops program if a command fails and prints debug info
    Return:
        A tuple with STDOUT, STDERR and the return code
    """
    global SCRIPT_LOG
    process = Popen(command, stdout=PIPE, stderr=PIPE, close_fds=False,
                    shell=True)

    stdoutdata, stderrdata = process.communicate()
    ret = process.returncode
    msg = f'CMD: {command}\n'
    msg += f'RET: {ret}\n'
    if ret != 0 and fail is True:
        msg += f'STDOUT:\n{stdoutdata.decode()}\n'
        msg += f'STDERR:\n{stderrdata.decode()}\n'
        if CONFIG_DEBUG_SCMD:
            print(msg)
        exit(1)
    if SCRIPT_LOG != '':
        with open(SCRIPT_LOG, 'a') as logf:
            logf.write(msg)

    return stdoutdata.decode(), stderrdata.decode(), ret


def bsp_comand(bsp_path, nfp_pci, bsp_cmd, options=""):
    """Helper to execute bsp commands"""
    cmd = f"{bsp_path}/{bsp_cmd} -Z {nfp_pci} {options}"
    out, err, ret = scmd(cmd, fail=False)
    return out, err, ret


def write_logfile(input_strings, filename, flags='w'):
    """Write strings log into corresponding file"""
    with open(filename, flags) as f:
        f.write(input_strings)


def common_data(outDir):
    """Output data that needs to be stored separately"""

    # Log firmware path where driver loads from. Could be symlinked files
    libFw, _, _ = scmd('ls -ogR --time-style="+" /lib/firmware/netronome/',
                       fail=False)
    libFw_path = f'{outDir}/lib_firmware.log'
    write_logfile(libFw, libFw_path)

    # Log firmware path where package installs places files
    optFw, _, _ = scmd('ls -ogR --time-style="+" /opt/firmware/netronome/',
                       fail=False)
    optFw_path = f'{outDir}/opt_firmware.log'
    write_logfile(optFw, optFw_path)

    nfp_pcis = get_nfps()
    nfp_pcis_path = f'{outDir}/nfp_pcis.log'
    write_logfile(str(nfp_pcis), nfp_pcis_path)

    for line in nfp_pcis:
        # Driver might not be bound to nfp, in which case none of the below
        # data can be gathered. Log this and move to next nfp pci device.
        if not os.path.exists(f'/sys/bus/pci/drivers/nfp/{line}/net'):
            no_ndev_log_path = f'{outDir}/{line}_no_netdevs'
            msg = f"No netdevs seem to exist for {line}. Most likely cause "
            msg += "is that the device is not bound to the nfp driver\n"
            write_logfile(msg, no_ndev_log_path)
            continue

        ndevs = "\n".join(os.listdir(f'/sys/bus/pci/drivers/nfp/{line}/net'))
        ndevs_path = f'{outDir}/ndevs.log'
        write_logfile(ndevs, ndevs_path, flags='a')
        ndev = os.listdir(f'/sys/bus/pci/drivers/nfp/{line}/net')[0]
        # set ethtool loglevel to 0
        scmd(f'ethtool -W {ndev} 0', fail=False)
        scmd(f'ethtool -w {ndev} data {outDir}/ethtool_{ndev}_debug_L0.dat',
             fail=False)
        # set ethtool loglevel to 1
        scmd(f'ethtool -W {ndev} 1', fail=False)
        scmd(f'ethtool -w {ndev} data {outDir}/ethtool_{ndev}_debug_L1.dat',
             fail=False)

    # devlink dev list
    dev_list, _, _ = scmd('devlink dev list', fail=False)
    dev_list_path = f'{outDir}/devlink_dev_list'
    write_logfile(dev_list, dev_list_path)

    with open(dev_list_path) as f:
        for port in f:
            port = port.replace('\n', '')
            pName = port.replace('/', '-')
            # devlink dev info <pci_address>
            dev_info, _, _ = scmd(f'devlink dev info {port}', fail=False)
            dev_info_path = f'{outDir}/devlink_dev_info_{pName}'
            write_logfile(dev_info, dev_info_path)

    # sensors
    sensors_info, _, _ = scmd('sensors -n', fail=False)
    sensors_path = f'{outDir}/sensors'
    write_logfile(sensors_info, sensors_path)

    # lspci
    lspci_info, _, _ = scmd('lspci -vvv', fail=False)
    lspci_path = f'{outDir}/lspci'
    write_logfile(lspci_info, lspci_path)

    # lspci - tree view
    lspci_tv_info, _, _ = scmd('lspci -tv', fail=False)
    lspci_tv_path = f'{outDir}/lspci_tv'
    write_logfile(lspci_tv_info, lspci_tv_path)

    # firewalld
    firewalld_info, _, _ = scmd('systemctl status firewalld', fail=False)
    firewalld_path = f'{outDir}/systemctl_status_firewalld'
    write_logfile(firewalld_info, firewalld_path)

    # irqbalance
    irqbalance_info, _, _ = scmd('systemctl status irqbalance', fail=False)
    irqbalance_path = f'{outDir}/systemctl_status_irqbalance'
    write_logfile(irqbalance_info, irqbalance_path)

    # lscpu
    lscpu_info, _, _ = scmd('lscpu', fail=False)
    lscpu_path = f'{outDir}/lscpu'
    write_logfile(lscpu_info, lscpu_path)

    # lshw
    lshw_info, _, _ = scmd('lshw -class network -businfo', fail=False)
    lshw_path = f'{outDir}/lshw'
    write_logfile(lshw_info, lshw_path)

    # /etc/*release
    etc_release_info, _, _ = scmd('ls -l /etc/*release && cat /etc/*release',
                                  fail=False)
    etc_release_path = f'{outDir}/etc_release'
    write_logfile(etc_release_info, etc_release_path)

    # numactl --hardware
    numactl_info, _, _ = scmd('numactl --hardware', fail=False)
    numactl_path = f'{outDir}/numactl_hardware'
    write_logfile(numactl_info, numactl_path)

    # The next two commands tries to capture the installed packages on
    # the system. Adding system detection that is stable accross all
    # OS's is complicated and error prone, so instead just log both
    # std_out and std_err for both 'rpm -qa' and 'dpkg -l'

    # rpm -qa
    rpm_info, err, _ = scmd('rpm -qa', fail=False)
    rpm_info += f"\n{err}"
    rpm_path = f'{outDir}/rpm_package_list'
    write_logfile(rpm_info, rpm_path)

    # dpkg -l
    deb_info, err, _ = scmd('dpkg -l', fail=False)
    deb_info += f"\n{err}"
    deb_path = f'{outDir}/deb_package_list'
    write_logfile(deb_info, deb_path)

    # dmidecode
    for dmi in ["bios", "system", "baseboard", "chassis", "processor",
                "memory", "cache", "connector", "slot"]:

        dmi_info, _, _ = scmd(f'dmidecode -t {dmi}', fail=False)
        dmi_path = f'{outDir}/dmidecode_{dmi}'
        write_logfile(dmi_info, dmi_path)

    # /proc/meminfo
    meminfo_path = f'{outDir}/proc_meminfo'
    try:
        shutil.copy2('/proc/meminfo', meminfo_path)
    except Exception:
        write_logfile('Error reading /proc/meminfo', meminfo_path)

    # /proc/cpuinfo
    cpuinfo_path = f'{outDir}/proc_cpuinfo'
    try:
        shutil.copy2('/proc/cpuinfo', cpuinfo_path)
    except Exception:
        write_logfile('Error reading /proc/cpuinfo', cpuinfo_path)

    # /var/boot.log
    bootlog_path = f'{outDir}/boot.log'
    try:
        shutil.copy2('/var/log/boot.log', bootlog_path)
    except Exception:
        write_logfile('Error reading /var/log/boot.log', bootlog_path)

    # history
    home_dir = os.path.expanduser('~')
    history_infile = f'{home_dir}/.bash_history'
    history_outfile = f'{outDir}/history'
    try:
        shutil.copy2(history_infile, history_outfile)
    except Exception:
        write_logfile(f'{history_infile}/ not found',
                      history_outfile)

    # /etc/udev/rules.d
    udev_indir = '/etc/udev/rules.d'
    udev_outdir = f'{outDir}/etc_udev_rules.d'
    try:
        shutil.copytree(udev_indir, udev_outdir)
    except Exception:
        os.makedirs(udev_outdir, exist_ok=True)
        write_logfile(f'Could not backup {udev_indir}\n',
                      f'{udev_outdir}/udev_rules.d_err')

    # /usr/lib/udev/rules.d
    udev_indir = '/usr/lib/udev/rules.d'
    udev_outdir = f'{outDir}/usr_lib_udev_rules.d'
    try:
        shutil.copytree(udev_indir, udev_outdir)
    except Exception:
        os.makedirs(udev_outdir, exist_ok=True)
        write_logfile(f'Could not backup {udev_indir}\n',
                      f'{udev_outdir}/usr_udev_rules.d_err')

    # /etc/sysconfig/network-scripts
    nscripts_indir = '/etc/sysconfig/network-scripts'
    nscripts_outdir = f'{outDir}/network-scripts'
    try:
        shutil.copytree(nscripts_indir, nscripts_outdir)
    except Exception:
        os.makedirs(nscripts_outdir, exist_ok=True)
        write_logfile(f'Could not backup {nscripts_indir}\n',
                      f'{nscripts_outdir}/nscripts_err')


def sos_data(outDir):
    """Output data from sos."""
    sos_cmd = get_sosreport_cmd()
    if sos_cmd == "":
        return False
    loaded_plugins = "-o networking -o logs -o kernel"
    loaded_plugins_path = f'{outDir}/loaded_plugins'
    write_logfile(f"{loaded_plugins}\n", loaded_plugins_path)
    # sos report -o networking -o logs -o kernel --batch --tmp-dir --build
    print("Generate DATA with sos")
    scmd(f'{sos_cmd} {loaded_plugins} --batch --tmp-dir {outDir} --build',
         fail=False)
    return True


def self_data(outDir):
    """Output data similar to sos but add self"""
    ethDir = f'{outDir}/ethtool'
    os.makedirs(ethDir, exist_ok=True)
    # ls /sys/class/net
    net_list, _, _ = scmd('ls /sys/class/net', fail=False)
    net_list_path = f'{ethDir}/net_list'
    write_logfile(net_list, net_list_path)

    with open(net_list_path) as f:
        for item in f:
            item = item.replace('\n', '')
            # ethtool <port>
            ethtool, _, _ = scmd(f'ethtool {item}', fail=False)
            ethtool_path = f'{ethDir}/ethtool_{item}'
            write_logfile(ethtool, ethtool_path)
            # ethtool -a <port>
            ethtool_a, _, _ = scmd(f'ethtool -a {item}', fail=False)
            ethtool_a_path = f'{ethDir}/ethtool_-a_{item}'
            write_logfile(ethtool_a, ethtool_a_path)
            # ethtool -c <port>
            ethtool_c, _, _ = scmd(f'ethtool -c {item}', fail=False)
            ethtool_c_path = f'{ethDir}/ethtool_-c_{item}'
            write_logfile(ethtool_c, ethtool_c_path)
            # ethtool -d <port>
            ethtool_d, _, _ = scmd(f'ethtool -d {item}', fail=False)
            ethtool_d_path = f'{ethDir}/ethtool_-d_{item}'
            write_logfile(ethtool_d, ethtool_d_path)
            # ethtool -k <port>
            ethtool_k, _, _ = scmd(f'ethtool -k {item}', fail=False)
            ethtool_k_path = f'{ethDir}/ethtool_-k_{item}'
            write_logfile(ethtool_k, ethtool_k_path)
            # ethtool -i <port>
            ethtool_i, _, _ = scmd(f'ethtool -i {item}', fail=False)
            ethtool_i_path = f'{ethDir}/ethtool_-i_{item}'
            write_logfile(ethtool_i, ethtool_i_path)
            # ethtool -P <port>
            ethtool_P, _, _ = scmd(f'ethtool -P {item}', fail=False)
            ethtool_P_path = f'{ethDir}/ethtool_-P_{item}'
            write_logfile(ethtool_P, ethtool_P_path)
            # ethtool --phy-statistics <port>
            ethtool_phy_s, _, _ = scmd(f'ethtool --phy-statistics {item}',
                                       fail=False)
            ethtool_phy_s_path = f'{ethDir}/ethtool_-phy-statistics_{item}'
            write_logfile(ethtool_phy_s, ethtool_phy_s_path)
            # ethtool -S <port>
            ethtool_S, _, _ = scmd(f'ethtool -S {item}', fail=False)
            ethtool_S_path = f'{ethDir}/ethtool_-S_{item}'
            write_logfile(ethtool_S, ethtool_S_path)
            # ethtool -g <port>
            ethtool_g, _, _ = scmd(f'ethtool -g {item}', fail=False)
            ethtool_g_path = f'{ethDir}/ethtool_-g_{item}'
            write_logfile(ethtool_g, ethtool_g_path)
            # ethtool -T <port>
            ethtool_T, _, _ = scmd(f'ethtool -T {item}', fail=False)
            ethtool_T_path = f'{ethDir}/ethtool_-T_{item}'
            write_logfile(ethtool_T, ethtool_T_path)
            # ethtool -m <port>
            ethtool_m, _, _ = scmd(f'ethtool -m {item}', fail=False)
            ethtool_m_path = f'{ethDir}/ethtool_-m_{item}'
            write_logfile(ethtool_m, ethtool_m_path)
            # ethtool --show-eee <port>
            ethtool_show_eee, _, _ = scmd(f'ethtool --show-eee {item}',
                                          fail=False)
            ethtool_show_eee_path = f'{ethDir}/ethtool_-show-eee_{item}'
            write_logfile(ethtool_show_eee, ethtool_show_eee_path)
            # ethtool --show-fec <port>
            ethtool_show_fec, _, _ = scmd(f'ethtool --show-fec {item}',
                                          fail=False)
            ethtool_show_fec_path = f'{ethDir}/ethtool_-show-fec_{item}'
            write_logfile(ethtool_show_fec, ethtool_show_fec_path)
            # ethtool --show-priv-flags <port>
            ethtool_show_priv, _, _ = scmd(f'ethtool --show-priv-flags {item}',
                                           fail=False)
            ethtool_priv_path = f'{ethDir}/ethtool_-show-priv-flags_{item}'
            write_logfile(ethtool_show_priv, ethtool_priv_path)
            # tc -s filter show dev <port>
            tc_filter, _, _ = scmd(f'tc -s filter show dev {item}', fail=False)
            tc_filter_path = f'{ethDir}/tc_-s_filter_{item}'
            write_logfile(tc_filter, tc_filter_path)
            # tc -s filter show dev <port> ingress
            tc_ingress, _, _ = scmd(f'tc -s filter show dev {item} ingress',
                                    fail=False)
            tc_ingress_path = f'{ethDir}/tc_-s_filter_{item}_ingress'
            write_logfile(tc_ingress, tc_ingress_path)

    # nstat -zas
    nstat_zas, _, _ = scmd('nstat -zas', fail=False)
    nstat_zas_path = f'{ethDir}/nstat_-zas'
    write_logfile(nstat_zas, nstat_zas_path)
    # netstat -W -neopa
    netstat_W_neopa, _, _ = scmd('netstat -W -neopa', fail=False)
    netstat_W_neopa_path = f'{ethDir}/netstat_-W_-neopa'
    write_logfile(netstat_W_neopa, netstat_W_neopa_path)
    # netstat -W -agn
    netstat_W_agn, _, _ = scmd('netstat -W -agn', fail=False)
    netstat_W_agn_path = f'{ethDir}/netstat_-W_-agn'
    write_logfile(netstat_W_agn, netstat_W_agn_path)
    # netstat -s
    netstat_s, _, _ = scmd('netstat -s', fail=False)
    netstat_s_path = f'{ethDir}/netstat_s'
    write_logfile(netstat_s, netstat_s_path)
    # ip -s neigh show
    ip_s_neigh_show, _, _ = scmd('ip -s neigh show', fail=False)
    ip_s_neigh_show_path = f'{ethDir}/ip_-s_neigh_show'
    write_logfile(ip_s_neigh_show, ip_s_neigh_show_path)
    # ip -s -d link
    ip_sd_link, _, _ = scmd('ip -s -d link', fail=False)
    ip_sd_link_path = f'{ethDir}/ip_-s_-d_link'
    write_logfile(ip_sd_link, ip_sd_link_path)
    # ip route show table all
    ip_r_show_tb, _, _ = scmd('ip route show table all', fail=False)
    ip_r_show_tb_path = f'{ethDir}/ip_route_show_table_all'
    write_logfile(ip_r_show_tb, ip_r_show_tb_path)
    # ip -o addr
    ip_o_addr, _, _ = scmd('ip -o addr', fail=False)
    ip_o_addr_path = f'{ethDir}/ip_-o_addr'
    write_logfile(ip_o_addr, ip_o_addr_path)
    # ip netns
    ip_netns, _, _ = scmd('ip netns', fail=False)
    ip_netns_path = f'{ethDir}/ip_netns'
    write_logfile(ip_netns, ip_netns_path)
    # ip neigh show nud noarp
    ip_nshow_nud, _, _ = scmd('ip neigh show nud noarp', fail=False)
    ip_nshow_nud_path = f'{ethDir}/ip_neigh_show_nud_noarp'
    write_logfile(ip_nshow_nud, ip_nshow_nud_path)
    # ip mroute show
    ip_mroute, _, _ = scmd('ip mroute show', fail=False)
    ip_mroute_path = f'{ethDir}/ip_mroute_show'
    write_logfile(ip_mroute, ip_mroute_path)
    # ip maddr show
    ip_maddr, _, _ = scmd('ip maddr show', fail=False)
    ip_maddr_path = f'{ethDir}/ip_maddr_show'
    write_logfile(ip_maddr, ip_maddr_path)
    # ip -d route show cache
    ip_droute, _, _ = scmd('ip -d route show cache', fail=False)
    ip_droute_path = f'{ethDir}/ip_-d_route_show_cache'
    write_logfile(ip_droute, ip_droute_path)
    # ip -d address
    ip_daddr, _, _ = scmd('ip -d address', fail=False)
    ip_daddr_path = f'{ethDir}/ip_-d_address'
    write_logfile(ip_daddr, ip_daddr_path)
    # ip -d -6 route show cache
    ip_d6route, _, _ = scmd('ip -d -6 route show cache', fail=False)
    ip_d6route_path = f'{ethDir}/ip_-d_-6_route_show_cache'
    write_logfile(ip_d6route, ip_d6route_path)
    # ip -6 rule
    ip_6_rule, _, _ = scmd('ip -6 rule', fail=False)
    ip_6_rule_path = f'{ethDir}/ip_-d_-6_route_show_cache'
    write_logfile(ip_6_rule, ip_6_rule_path)
    # ip -6 route show table all
    ip_6_route_tb, _, _ = scmd('ip -6 route show table all', fail=False)
    ip_6_route_tb_path = f'{ethDir}/ip_-6_route_show_table_all'
    write_logfile(ip_6_route_tb, ip_6_route_tb_path)
    # ip -4 rule
    ip_4_rule, _, _ = scmd('ip -4 rule', fail=False)
    ip_4_rule_path = f'{ethDir}/ip_-4_rule'
    write_logfile(ip_4_rule, ip_4_rule_path)
    # ifenslave -a
    ifenslave_a, _, _ = scmd('ifenslave -a', fail=False)
    ifenslave_a_path = f'{ethDir}/ifenslave_-a'
    write_logfile(ifenslave_a, ifenslave_a_path)

    knlDir = f'{outDir}/kernel'
    os.makedirs(knlDir, exist_ok=True)
    # cat /etc/hostname
    etc_hostname, _, _ = scmd('cat /etc/hostname', fail=False)
    etc_hostname_path = f'{knlDir}/etc_hostname.log'
    write_logfile(etc_hostname, etc_hostname_path)
    # journalctl -k
    journalctl_k, _, _ = scmd('journalctl -k', fail=False)
    journalctl_k_path = f'{knlDir}/journalctl_-k.log'
    write_logfile(journalctl_k, journalctl_k_path)
    # uname -a
    uname_a, _, _ = scmd('uname -a', fail=False)
    uname_a_path = f'{knlDir}/uname_-a.log'
    write_logfile(uname_a, uname_a_path)
    # cat /etc/*release
    etc_release, _, _ = scmd('cat /etc/*release', fail=False)
    etc_release_path = f'{knlDir}/etc_release.log'
    write_logfile(etc_release, etc_release_path)
    # modinfo nfp
    modinfo_nfp, _, _ = scmd('modinfo nfp', fail=False)
    modinfo_nfp_path = f'{knlDir}/modinfo_nfp.log'
    write_logfile(modinfo_nfp, modinfo_nfp_path)
    # lsmod
    lsmod, _, _ = scmd('lsmod', fail=False)
    lsmod_path = f'{knlDir}/lsmod.log'
    write_logfile(lsmod, lsmod_path)
    # cat /proc/cmdline
    cmdline, _, _ = scmd('cat /proc/cmdline', fail=False)
    cmdline_path = f'{knlDir}/cmdline.log'
    write_logfile(cmdline, cmdline_path)
    # ls /sys/bus/pci/drivers/nfp/module/parameters/
    oot_para_path = '/sys/bus/pci/drivers/nfp/module/parameters/'
    oot_parameters, _, _ = scmd(f'ls {oot_para_path}', fail=False)
    oot_parameters_path = f'{knlDir}/oot_parameters.log'
    write_logfile(oot_parameters, oot_parameters_path)
    # cat /boot/config-$(uname -r)
    kernel_config, _, _ = scmd('cat /boot/config-$(uname -r)', fail=False)
    kernel_config_path = f'{knlDir}/kernel_config.log'
    write_logfile(kernel_config, kernel_config_path)

    if 'CentOS' in etc_release:
        # lsinitrd
        lsinitrd, _, _ = scmd('lsinitrd', fail=False)
        lsinitrd_path = f'{knlDir}/lsinitrd.log'
        write_logfile(lsinitrd, lsinitrd_path)
    elif 'Ubuntu' in etc_release:
        # sinitramfs /boot/initrd.img
        lsinitramfs, _, _ = scmd('lsinitramfs /boot/initrd.img', fail=False)
        lsinitramfs_path = f'{knlDir}/lsinitramfs.log'
        write_logfile(lsinitramfs, lsinitramfs_path)


def check_cpp_access(outDir):
    """Checks if the driver was loaded with cpp access enabled"""
    cpp_param = 0
    bspDir = f'{outDir}/bsp_logs'
    os.makedirs(bspDir, exist_ok=True)

    nfp_module_path = '/sys/bus/pci/drivers/nfp/module'
    cpp_param_path = f'{nfp_module_path}/parameters/nfp_dev_cpp'

    # Check if in-tree driver is loaded
    if not os.path.exists(cpp_param_path):
        fail_msg_path = f'{bspDir}/fail_intree_module'
        message = f'{nfp_module_path} does not exist\n'
    else:
        with open(cpp_param_path) as cpp_file:
            cpp_param = cpp_file.read()
        cpp_param = int(cpp_param)
        if cpp_param == 0:
            fail_msg_path = f'{bspDir}/fail_cpp_not_enabled'
            message = "driver not loaded with cpp_access enabled\n"

    # Check if driver is loaded at all, and override error
    # message if not loaded
    if not os.path.exists(nfp_module_path):
        fail_msg_path = f'{bspDir}/fail_module_not_loaded'
        message = f'{nfp_module_path} missing, driver not loaded or built-in\n'

    # If cpp_param did not get set write to log and exit
    if cpp_param == 0:
        write_logfile(message, fail_msg_path)
        return False
    return True


def filter_multi_pf(pci_list):
    """Returns a filtered list, containing only single pci-address
    for every card.

    Entries in list is in the normal DBDF <0000:aa:00.0>.
    If there are multi-PF devices then there will be two (or more)
    entries for a card, e.g:
        0000:aa:00.0
        0000:aa:00.1

    This function will return a list containing only one entry per
    card.
    """

    # This will be the new returned list
    filt_pci = []
    # This list is temporary to keep track of previously seen devices
    BDF_list = []

    for pci in pci_list:
        BDF = pci.split('.')[0]
        if BDF not in BDF_list:
            BDF_list.append(BDF)
            filt_pci.append(pci)
    return filt_pci


def netdev_to_pci(netdev):
    """Takes in a netdev name, and returns an pci-address if possible,
    otherwise None
    """
    try:
        pci_path = os.readlink(f'/sys/class/net/{netdev}/device')
        pci_path = os.path.basename(pci_path)
        return pci_path
    except Exception:
        return None


def get_enabled_islands(bsp_path, pci):
    """Parses 'chip.island' and return list of enabled islands"""

    isl_list = []
    out, _, _ = bsp_comand(bsp_path, pci, 'nfp-hwinfo', options='chip.island')
    isl_hex_str = out.split('=')[1]
    # isl_hex_str in format '0x1001f13000112'
    isl_int = int(isl_hex_str, 16)

    # Bit positions correspond with enabled islands, convert to binary
    # representation in order to parse
    isl_bin_str = "{:b}".format(isl_int)
    for nr, me in enumerate(isl_bin_str[::-1]):
        if me == '1':
            isl_list.append(nr)
    return isl_list


def parse_rtsym_table(sym_str):
    """Parses the output of the 'nfp-rtsym -L' command
        Name                    Resource   Address      Size
        BLM_EMU_RING_INIT_CHK_0 <generic>  0x0000000000 0x0000000001
        nfd_in_ring_nums0       <generic>  0x0000000000 0x0000000001

    Returns a dictionary of dictionaries:
        { "BLM_EMU..." :
            { "Resource": <generic>,
              "Address": 0x000...,
              "Size": int(0x000...),
            },
          "nfd_in...":
          {
          }
        }
    """
    rtsym_dict = {}
    # Iterate through lines, but skip the heading
    for line in sym_str.strip().split("\n")[1:]:
        name, res, addr, size = line.strip().split()
        rtsym_dict[name] = {
            "Resource": res,
            "Address": addr,
            "Size": int(size, 16)
            }
    return rtsym_dict


def filter_xpb_nbi(xpb_in):
    """Dumping ALL the nbi xpb/m information takes very long and does
    sometime leave the card in a bad state. Limit to only specific
    information and return a list of commands.
    """
    nbi_cmd_list = []
    for nbiXPB in NbiXPB_to_dump:
        cmd = f'{xpb_in}NbiTopXpbMap.{nbiXPB}'
        nbi_cmd_list.append(cmd)

    return nbi_cmd_list


def write_rtsym(bsp_path, pci, rtsym_dict, key, logpath):
    """Simple wrapper to log rtsym command"""
    head = f'rtsym={key},size={rtsym_dict[key]["Size"]}\n'
    write_logfile(head, logpath, "a")
    dat, _, _ = bsp_comand(bsp_path, pci, 'nfp-rtsym', options=f'-v {key}')
    write_logfile(dat, logpath, "a")


def collect_bsp_data(bsp_path, outDir, args):
    """Collect low level debug information using BSP tools"""

    bspDir = f"{outDir}/bsp_logs"
    os.makedirs(bspDir, exist_ok=True)
    all_nfp_pcis = get_nfps()
    nfp_pcis = filter_multi_pf(all_nfp_pcis)

    # Collect generic BSP info for all nfp's
    for pci in nfp_pcis:
        # Each entry a tuple of the command to run [0], and the option
        # flags to use [1]
        cmds = (
            ('nfp-arm', '-D'),
            ('nfp-fis', '-b1 list'),
            ('nfp-hwinfo', ''),
            ('nfp-media', ''),
            ('nfp-nsp', '-E'),
            ('nfp-phymod', ''),
            ('nfp-res', '-L'),
            ('nfp-temp', ''),
            ('nic-power', ''),
        )
        for cmd, opt in cmds:
            out, _, _ = bsp_comand(bsp_path, pci, cmd, options=opt)
            fpath = f"{bspDir}/{pci}_{cmd}"
            # If option is specified add this to the output filename
            if opt:
                fpath = f"{fpath}_{opt}"
            out_str = f"{out.strip()}\n"
            write_logfile(out_str, fpath, "w")

        # Log the enabled NFP islands
        isls = get_enabled_islands(bsp_path, pci)
        fpath = f"{bspDir}/{pci}_islands"
        out_str = (",".join(ISL_MAP[x][0] for x in isls))
        out_str += "\n"
        write_logfile(out_str, fpath, "w")

        # Check if gearbox is in use, and log additional info
        hwinfo, _, _ = bsp_comand(bsp_path, pci, 'nfp-hwinfo', options='')
        hwinfo_d = {}
        for line in hwinfo.strip().split('\n'):
            key, val = line.strip().split('=')
            hwinfo_d[key] = val

        if 'phy0.mvl5113' in hwinfo_d.keys():
            # Get nr of ports:
            port_count = 0
            for k in hwinfo_d.keys():
                if '.nbi' in k:
                    port_count += 1

            # Log mdio commands
            fpath = f"{bspDir}/{pci}_mdio"
            cmd_list = [
                '7.0000',  # Lane Auto-Negotiation Control
                '7.0001',  # Lane Auto-Negotiation Status
                '7.0010',  # Auto-Negotiation Advertisement Register 1
                '7.0011',  # Auto-Negotiation Advertisement Register 2
                '7.0012',  # Auto-Negotiation Advertisement Register 3
                '3.F000',  # Laneside Interface Mode Select Sub Port 0
                '4.F000',  # Hostside Interface Mode Select Sub Port 0
                ]
            data = ''
            for mdio_cmd in cmd_list:
                for port_nr in range(0, port_count):
                    cmd = f'2 {port_nr} {mdio_cmd}'
                    out, _, _ = bsp_comand(bsp_path, pci, 'nfp-mdio',
                                           options=cmd)
                    data = f'{data}nfp-mdio {cmd}={out}\n'
            write_logfile(data, fpath, "a")

    # Collect firmware specific logs if the interface was specified, otherwise
    # return now
    netdev = args.iface
    if netdev == '':
        return

    # Exit early if the device is not in the pci id list
    ndev_pci = netdev_to_pci(netdev)
    if ndev_pci is None or ndev_pci not in all_nfp_pcis:
        print(f"{ndev_pci} for {netdev} does not seem to be in"
              f"the detected nfp devices: {all_nfp_pcis.keys()}")
        return
    print(f'Collecting low level information for {netdev}, pci:{ndev_pci}')

    # ME_csr dumps
    MECSRS = [
        "ActCtxStatus",
        "CtxEnables",
        "CSRCtxPtr",
        "UstorErrStat",
        "ALUOut",
        "CtxArbCtrl",
        "CondCodeEn",
        "RegErrStatus",
        "LMErrStatus",
        "LMeccErrorMask",
    ]

    isls = get_enabled_islands(bsp_path, ndev_pci)
    for isl in isls:
        nr_mes = ISL_MAP[isl][1]
        if nr_mes == 0:
            continue

        fpath = f"{bspDir}/{ndev_pci}_{netdev}_MECSRS"
        data = ''
        for MECSR in MECSRS:
            me_str = f"i{isl}.me{{0..{nr_mes - 1}}}"
            cmd = f'mecsr:{me_str}.{MECSR}'
            # e.g: cmd=mecsr:i1.me{0..3}.CtxEnables
            out, _, _ = bsp_comand(bsp_path, ndev_pci, 'nfp-reg', options=cmd)
            data = f'{data}{out}\n'
        write_logfile(data, fpath, "w")

        # While in this loop also collect cls information
        fpath = f"{bspDir}/{ndev_pci}_{netdev}_cls"
        cmd = f'cls:i{isl}'
        out, _, _ = bsp_comand(bsp_path, ndev_pci, 'nfp-reg', options=cmd)
        write_logfile(out, fpath, "a")

    # Collect xpb and xpbm information.
    for xpb_cmd in ['xpb', 'xpbm']:
        # Get list of valid targets. The output needs some filtering as some
        # duplication is present
        out, _, _ = bsp_comand(bsp_path, ndev_pci, 'nfp-reg',
                               options=f"-l {xpb_cmd}")

        # Iterate through output lines, and only add to list if not already
        # present. Also filter out some of the commands that takes a long time.
        cmd_list = []
        for line in out.split():
            cmd = line.strip()
            # Skip all Nbi xpbm commands
            if "Nbi" in cmd and xpb_cmd == 'xpbm':
                continue
            # Filter Nbi xpb commands
            elif "Nbi" in cmd and xpb_cmd == 'xpb':
                cmd_list += filter_xpb_nbi(cmd)
            # Add all other commands
            elif cmd not in cmd_list:
                cmd_list.append(cmd)

        # Execute commands from the list and log to file
        for xpb in cmd_list:
            fpath = f"{bspDir}/{ndev_pci}_{netdev}_{xpb_cmd.upper()}Map"
            out, _, _ = bsp_comand(bsp_path, ndev_pci, 'nfp-reg',
                                   options=f"{xpb}")
            write_logfile(out, fpath, "a")

    # Collect rtsym information
    out, _, _ = bsp_comand(bsp_path, ndev_pci, 'nfp-rtsym', options="-L")
    rtsym_dict = parse_rtsym_table(out)

    fpath = f'{bspDir}/{ndev_pci}_{netdev}_rtsyms'
    for sym in rtsyms_to_dump:
        if sym in rtsym_dict.keys():
            write_rtsym(bsp_path, ndev_pci, rtsym_dict, sym, fpath)


def get_sosreport_cmd():
    """ Check for the existance of either 'sos' or 'sosreport'

    Return 'sos report' if tool is 'sos'
    Return 'sosreport' if tools is 'sosreport'
    Return empty string if neither was found
    """

    # Try 'sosreport'
    sos_path = shutil.which("sosreport")
    if sos_path is not None:
        return sos_path

    # Try 'sos'
    sos_path = shutil.which("sos")
    if sos_path is not None:
        return f"{sos_path} report"

    return ""


def main():
    """Call the main program functions."""
    global SCRIPT_LOG

    args = parse_cmdline()

    missing_tools = []
    for tool in REQ_SHELL_TOOLS:
        if shutil.which(tool) is None:
            missing_tools.append(tool)
    if missing_tools:
        print(f"Missing tools: {', '.join(missing_tools)}\n"
              "Please ensure these are installed on the system.")
        exit(1)

    baseDir = os.path.abspath(args.logdir)
    date, _, _ = scmd('date -u +%Y.%m.%d.%H%M')
    date = date.replace('\n', '')
    outDir = f'{baseDir}/nfp_troubleshoot_gather_{date}'
    try:
        os.makedirs(outDir, exist_ok=True)
    except PermissionError:
        print(f'Error: Seems like {baseDir} does not have write permissions. '
              'Exiting.')
        exit(1)
    SCRIPT_LOG = f'{outDir}/script_cmds.log'

    is_root = False
    if os.getuid() == 0:
        is_root = True

    if is_root is False:
        print('Warn: It is recommended to run the script with root access. '
              'Some information will not be logged without root permissions.')
        with open(SCRIPT_LOG, 'a') as logf:
            logf.write('Script NOT executed as root\n')

    common_data(outDir)
    if not sos_data(outDir):
        print("sos is not installed. Suggest install")
        print("Generate DATA without sos")
        self_data(outDir)

    # BSP tools require sudo access, only execute if running as superuser
    if is_root is True:
        bsp_path = check_bsp_tools_installed()
        if bsp_path != '' and check_cpp_access(outDir):
            collect_bsp_data(bsp_path, outDir, args)

    # Tarring up logs
    # Clear SCRIPT_LOG, since this last step cannot be logged
    # while --remove-files is used with tar
    SCRIPT_LOG = ''
    scmd(f'tar -zcvf {outDir}.tar.gz -C {baseDir} '
         f'nfp_troubleshoot_gather_{date} --remove-files')
    print(f'Done: {outDir}.tar.gz')

    # If script was run as sudo try and set permissions. Use same permissions
    # as logdir
    if is_root is True:
        stat_info = os.stat(baseDir)
        uid = stat_info.st_uid
        gid = stat_info.st_gid
        os.chown(f'{outDir}.tar.gz', uid, gid)


if __name__ == "__main__":
    main()
