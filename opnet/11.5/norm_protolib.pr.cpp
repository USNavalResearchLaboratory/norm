/* Process model C++ form file: norm_protolib.pr.cpp */
/* Portions of this file copyright 1992-2006 by OPNET Technologies, Inc. */



/* This variable carries the header into the object file */
const char norm_protolib_pr_cpp [] = "MIL_3_Tfile_Hdr_ 115A 30A op_runsim 7 44EC8462 44EC8462 1 apocalypse Jim@Hauser 0 0 none none 0 0 none 0 0 0 0 0 0 0 0 d50 3                                                                                                                                                                                                                                                                                                                                                                                                   ";
#include <string.h>



/* OPNET system definitions */
#include <opnet.h>



/* Header Block */

#include <OpnetNormProcess.h>
#include <oms_pr.h>
#include <oms_tan.h>
#include <udp_api.h>
#include <ip_rte_v4.h>
#include <ip_addr_v4.h>
#include <mgenVersion.h>

/* Define packet streams  */
#define INSTRM_FROM_UDP		0
#define INSTRM_FROM_MGEN	1
#define OUTSTRM_TO_MGEN		1

/*	Define a transition conditions              	*/
#define	SELF_NOTIF		(intrpt_type == OPC_INTRPT_SELF)
#define	END_SIM			(intrpt_type == OPC_INTRPT_ENDSIM)
#define	MSG_FROM_LOWER_LAYER	((intrpt_type == OPC_INTRPT_STRM) && (intrpt_strm == INSTRM_FROM_UDP)) 
#define	MSG_FROM_HIGHER_LAYER	((intrpt_type == OPC_INTRPT_STRM) && (intrpt_strm == INSTRM_FROM_MGEN)) 
#define TIMEOUT_EVENT	((intrpt_type == OPC_INTRPT_SELF) && (intrpt_code == SELF_INTRT_CODE_TIMEOUT_EVENT))

/* Forward Declarations */
void norm_conf_udp ();
void norm_host_addr ();

static const int MAX_SCRIPT = 128;
static const int MAX_LOG = 128;
static const int MAX_COMMAND = 256;
static const int MAXSTATFLOWS = 25;

/* End of Header Block */

#if !defined (VOSD_NO_FIN)
#undef	BIN
#undef	BOUT
#define	BIN		FIN_LOCAL_FIELD(_op_last_line_passed) = __LINE__ - _op_block_origin;
#define	BOUT	BIN
#define	BINIT	FIN_LOCAL_FIELD(_op_last_line_passed) = 0; _op_block_origin = __LINE__;
#else
#define	BINIT
#endif /* #if !defined (VOSD_NO_FIN) */



/* State variable definitions */
class norm_protolib_state
	{
	public:
		norm_protolib_state (void);

		/* Destructor contains Termination Block */
		~norm_protolib_state (void);

		/* State Variables */
		OpnetNormProcess	       		norm_proc                                       ;
		Objid	                  		my_id                                           ;
		Objid	                  		my_node_id                                      ;
		Objid	                  		my_pro_id                                       ;
		Objid	                  		my_udp_id                                       ;
		Objid	                  		my_tcp_id                                       ;
		Objid	                  		my_mgen_id                                      ;
		IpT_Address	            		my_ip_addr                                      ;
		IpT_Address	            		my_ip_mask                                      ;
		Prohandle	              		own_prohandle                                   ;
		OmsT_Pr_Handle	         		own_process_record_handle                       ;
		char	                   		pid_string [512]                                ;
		char	                   		node_name [40]                                  ;
		ProtoTimerMgr	          		timer                                           ;
		ProtoSocket*	           		udpsocket                                       ;
		ProtoSocket*	           		tcpsocket                                       ;
		ProtoSocket::Notifier*	 		udpnotifier                                     ;
		ProtoSocket::Notifier*	 		tcpnotifier                                     ;
		ProtoAddress	           		host_ipv4_addr                                  ;
		Stathandle	             		bits_rcvd_stathandle                            ;
		Stathandle	             		bitssec_rcvd_flow_stathandle[MAXSTATFLOWS]      ;
		Stathandle	             		bitssec_sent_flow_stathandle[MAXSTATFLOWS]      ;
		Stathandle	             		bitssec_rcvd_stathandle                         ;
		Stathandle	             		pkts_rcvd_stathandle                            ;
		Stathandle	             		pktssec_rcvd_flow_stathandle[MAXSTATFLOWS]      ;
		Stathandle	             		pktssec_sent_flow_stathandle[MAXSTATFLOWS]      ;
		Stathandle	             		pktssec_rcvd_stathandle                         ;
		Stathandle	             		bits_sent_stathandle                            ;
		Stathandle	             		bitssec_sent_stathandle                         ;
		Stathandle	             		pkts_sent_stathandle                            ;
		Stathandle	             		pktssec_sent_stathandle                         ;
		Stathandle	             		ete_delay_stathandle                            ;
		Stathandle	             		ete_delay_flow_stathandle[MAXSTATFLOWS]         ;
		Stathandle	             		bits_rcvd_gstathandle                           ;
		Stathandle	             		bitssec_rcvd_gstathandle                        ;
		Stathandle	             		pkts_rcvd_gstathandle                           ;
		Stathandle	             		pktssec_rcvd_gstathandle                        ;
		Stathandle	             		bits_sent_gstathandle                           ;
		Stathandle	             		bitssec_sent_gstathandle                        ;
		Stathandle	             		pkts_sent_gstathandle                           ;
		Stathandle	             		pktssec_sent_gstathandle                        ;
		Stathandle	             		ete_delay_gstathandle                           ;
		int	                    		udp_outstream_index                             ;
		int	                    		local_port                                      ;
		IpT_Address	            		dest_ip_addr                                    ;
		FILE*	                  		script_fp                                       ;
		int	                    		source                                          ;
		Ici*	                   		app_ici_ptr                                     ;

		/* FSM code */
		void norm_protolib (OP_SIM_CONTEXT_ARG_OPT);
		/* Diagnostic Block */
		void _op_norm_protolib_diag (OP_SIM_CONTEXT_ARG_OPT);

#if defined (VOSD_NEW_BAD_ALLOC)
		void * operator new (size_t) throw (VOSD_BAD_ALLOC);
#else
		void * operator new (size_t);
#endif
		void operator delete (void *);

		/* Memory management */
		static VosT_Obtype obtype;

	private:
		/* Internal state tracking for FSM */
		FSM_SYS_STATE
	};

VosT_Obtype norm_protolib_state::obtype = (VosT_Obtype)OPC_NIL;

#define pr_state_ptr            		((norm_protolib_state*) (OP_SIM_CONTEXT_PTR->_op_mod_state_ptr))
#define norm_proc               		pr_state_ptr->norm_proc
#define my_id                   		pr_state_ptr->my_id
#define my_node_id              		pr_state_ptr->my_node_id
#define my_pro_id               		pr_state_ptr->my_pro_id
#define my_udp_id               		pr_state_ptr->my_udp_id
#define my_tcp_id               		pr_state_ptr->my_tcp_id
#define my_mgen_id              		pr_state_ptr->my_mgen_id
#define my_ip_addr              		pr_state_ptr->my_ip_addr
#define my_ip_mask              		pr_state_ptr->my_ip_mask
#define own_prohandle           		pr_state_ptr->own_prohandle
#define own_process_record_handle		pr_state_ptr->own_process_record_handle
#define pid_string              		pr_state_ptr->pid_string
#define node_name               		pr_state_ptr->node_name
#define timer                   		pr_state_ptr->timer
#define udpsocket               		pr_state_ptr->udpsocket
#define tcpsocket               		pr_state_ptr->tcpsocket
#define udpnotifier             		pr_state_ptr->udpnotifier
#define tcpnotifier             		pr_state_ptr->tcpnotifier
#define host_ipv4_addr          		pr_state_ptr->host_ipv4_addr
#define bits_rcvd_stathandle    		pr_state_ptr->bits_rcvd_stathandle
#define bitssec_rcvd_flow_stathandle		pr_state_ptr->bitssec_rcvd_flow_stathandle
#define bitssec_sent_flow_stathandle		pr_state_ptr->bitssec_sent_flow_stathandle
#define bitssec_rcvd_stathandle 		pr_state_ptr->bitssec_rcvd_stathandle
#define pkts_rcvd_stathandle    		pr_state_ptr->pkts_rcvd_stathandle
#define pktssec_rcvd_flow_stathandle		pr_state_ptr->pktssec_rcvd_flow_stathandle
#define pktssec_sent_flow_stathandle		pr_state_ptr->pktssec_sent_flow_stathandle
#define pktssec_rcvd_stathandle 		pr_state_ptr->pktssec_rcvd_stathandle
#define bits_sent_stathandle    		pr_state_ptr->bits_sent_stathandle
#define bitssec_sent_stathandle 		pr_state_ptr->bitssec_sent_stathandle
#define pkts_sent_stathandle    		pr_state_ptr->pkts_sent_stathandle
#define pktssec_sent_stathandle 		pr_state_ptr->pktssec_sent_stathandle
#define ete_delay_stathandle    		pr_state_ptr->ete_delay_stathandle
#define ete_delay_flow_stathandle		pr_state_ptr->ete_delay_flow_stathandle
#define bits_rcvd_gstathandle   		pr_state_ptr->bits_rcvd_gstathandle
#define bitssec_rcvd_gstathandle		pr_state_ptr->bitssec_rcvd_gstathandle
#define pkts_rcvd_gstathandle   		pr_state_ptr->pkts_rcvd_gstathandle
#define pktssec_rcvd_gstathandle		pr_state_ptr->pktssec_rcvd_gstathandle
#define bits_sent_gstathandle   		pr_state_ptr->bits_sent_gstathandle
#define bitssec_sent_gstathandle		pr_state_ptr->bitssec_sent_gstathandle
#define pkts_sent_gstathandle   		pr_state_ptr->pkts_sent_gstathandle
#define pktssec_sent_gstathandle		pr_state_ptr->pktssec_sent_gstathandle
#define ete_delay_gstathandle   		pr_state_ptr->ete_delay_gstathandle
#define udp_outstream_index     		pr_state_ptr->udp_outstream_index
#define local_port              		pr_state_ptr->local_port
#define dest_ip_addr            		pr_state_ptr->dest_ip_addr
#define script_fp               		pr_state_ptr->script_fp
#define source                  		pr_state_ptr->source
#define app_ici_ptr             		pr_state_ptr->app_ici_ptr

/* These macro definitions will define a local variable called	*/
/* "op_sv_ptr" in each function containing a FIN statement.	*/
/* This variable points to the state variable data structure,	*/
/* and can be used from a C debugger to display their values.	*/
#undef FIN_PREAMBLE_DEC
#undef FIN_PREAMBLE_CODE
#  define FIN_PREAMBLE_DEC	norm_protolib_state *op_sv_ptr;
#if defined (OPD_PARALLEL)
#  define FIN_PREAMBLE_CODE	\
		op_sv_ptr = ((norm_protolib_state *)(sim_context_ptr->_op_mod_state_ptr));
#else
#  define FIN_PREAMBLE_CODE	op_sv_ptr = pr_state_ptr;
#endif


/* Function Block */

#if !defined (VOSD_NO_FIN)
enum { _op_block_origin = __LINE__ + 2};
#endif

/* Initialization function */

void norm_init ()
	{
	FIN (norm_init ())

	/* Initilaize the statistic handles to keep	*/
	/* track of traffic sent and sinked by this process.	*/
	bits_rcvd_stathandle 		= op_stat_reg ("NORM.Traffic Received (bits)",			OPC_STAT_INDEX_NONE, OPC_STAT_LOCAL);
	bitssec_rcvd_stathandle 	= op_stat_reg ("NORM.Traffic Received (bits/sec)",		OPC_STAT_INDEX_NONE, OPC_STAT_LOCAL);
	pkts_rcvd_stathandle 		= op_stat_reg ("NORM.Traffic Received (packets)",		OPC_STAT_INDEX_NONE, OPC_STAT_LOCAL);
	pktssec_rcvd_stathandle 	= op_stat_reg ("NORM.Traffic Received (packets/sec)",	OPC_STAT_INDEX_NONE, OPC_STAT_LOCAL);
	bits_sent_stathandle 		= op_stat_reg ("NORM.Traffic Sent (bits)",			OPC_STAT_INDEX_NONE, OPC_STAT_LOCAL);
	bitssec_sent_stathandle 	= op_stat_reg ("NORM.Traffic Sent (bits/sec)",		OPC_STAT_INDEX_NONE, OPC_STAT_LOCAL);
	pkts_sent_stathandle 		= op_stat_reg ("NORM.Traffic Sent (packets)",		OPC_STAT_INDEX_NONE, OPC_STAT_LOCAL);
	pktssec_sent_stathandle 	= op_stat_reg ("NORM.Traffic Sent (packets/sec)",	OPC_STAT_INDEX_NONE, OPC_STAT_LOCAL);
	ete_delay_stathandle		= op_stat_reg ("NORM.End-to-End Delay (seconds)",		OPC_STAT_INDEX_NONE, OPC_STAT_LOCAL);

	bits_rcvd_gstathandle 		= op_stat_reg ("NORM.Traffic Received (bits)",			OPC_STAT_INDEX_NONE, OPC_STAT_GLOBAL);
	bitssec_rcvd_gstathandle 	= op_stat_reg ("NORM.Traffic Received (bits/sec)",		OPC_STAT_INDEX_NONE, OPC_STAT_GLOBAL);
	pkts_rcvd_gstathandle 		= op_stat_reg ("NORM.Traffic Received (packets)",		OPC_STAT_INDEX_NONE, OPC_STAT_GLOBAL);
	pktssec_rcvd_gstathandle 	= op_stat_reg ("NORM.Traffic Received (packets/sec)",	OPC_STAT_INDEX_NONE, OPC_STAT_GLOBAL);
	bits_sent_gstathandle 		= op_stat_reg ("NORM.Traffic Sent (bits)",			OPC_STAT_INDEX_NONE, OPC_STAT_GLOBAL);
	bitssec_sent_gstathandle 	= op_stat_reg ("NORM.Traffic Sent (bits/sec)",		OPC_STAT_INDEX_NONE, OPC_STAT_GLOBAL);
	pkts_sent_gstathandle 		= op_stat_reg ("NORM.Traffic Sent (packets)",		OPC_STAT_INDEX_NONE, OPC_STAT_GLOBAL);
	pktssec_sent_gstathandle 	= op_stat_reg ("NORM.Traffic Sent (packets/sec)",	OPC_STAT_INDEX_NONE, OPC_STAT_GLOBAL);
	ete_delay_gstathandle		= op_stat_reg ("NORM.End-to-End Delay (seconds)",	OPC_STAT_INDEX_NONE, OPC_STAT_GLOBAL);

	/* Read norm comands specified via opnet model attributes */
	printf("NORM settings for %s:\n",node_name);
	char val[128];
	int tog;
	op_ima_obj_attr_get_str(my_id,"debug",128,val);		// debug level
	if (val[0])
		{
		printf(" debug =  %s\n",val);
		norm_proc.ProcessCommand("debug",val);
		}
	op_ima_obj_attr_get_str(my_id,"log",128,val);		// log file name
	if (val[0])
		{
		printf(" log =  %s\n",val);
		norm_proc.ProcessCommand("log",val);
		}
	op_ima_obj_attr_get_toggle(my_id,"trace",&tog);		// message tracing
	if (tog)
		{
		printf(" trace on\n",val);
		norm_proc.ProcessCommand("trace",NULL);
		}
	op_ima_obj_attr_get_str(my_id,"txloss",128,val);	// tx packet loss percent
	if (val[0])
		{
		printf(" txloss =  %s\n",val);
		norm_proc.ProcessCommand("txloss",val);
		}
	op_ima_obj_attr_get_str(my_id,"rxloss",128,val);	// rx packet loss percent
	if (val[0])
		{
		printf(" rxloss =  %s\n",val);
		norm_proc.ProcessCommand("rxloss",val);
		}
	op_ima_obj_attr_get_str(my_id,"address",128,val);	// session dest address
	if (val[0])
		{
		List* 	fieldlist;		
		printf(" address =  %s\n",val);
		norm_proc.ProcessCommand("address",val);
		fieldlist = op_prg_str_decomp ( val , "/" );
		dest_ip_addr = ip_address_create((const char *)op_prg_list_access(fieldlist, 0));
		local_port = atoi ((const char *)op_prg_list_access(fieldlist, 1));
		op_prg_list_free ( fieldlist ); 
		op_prg_mem_free ( fieldlist );
		}
	op_ima_obj_attr_get_str(my_id,"ttl",128,val);		// multicast ttl
	if (val[0])
		{
		printf(" ttl =  %s\n",val);
		norm_proc.ProcessCommand("ttl",val); // maybe shouldn't use norm_proc code
		}
	op_ima_obj_attr_get_str(my_id,"rate",128,val);		// tx rate
	if (val[0])
		{
		printf(" rate =  %s\n",val);
		norm_proc.ProcessCommand("rate",val);
		}
	op_ima_obj_attr_get_str(my_id,"cc",128,val);		// congestion control on/off
	if (val[0])
		{
		printf(" cc =  %s\n",val);
		norm_proc.ProcessCommand("cc",val);
		}
	op_ima_obj_attr_get_str(my_id,"backoff",128,val);	// backoff factor 'k' (maxBackoff = k * GRTT)
	if (val[0])
		{
		printf(" backoff =  %s\n",val);
		norm_proc.ProcessCommand("backoff",val);
		}
	op_ima_obj_attr_get_str(my_id,"interval",128,val);	// delay between tx objects
	if (val[0])
		{
		printf(" interval =  %s\n",val);
		norm_proc.ProcessCommand("interval",val);
		}
	op_ima_obj_attr_get_str(my_id,"repeat",128,val);	// number of times to repeat tx object set
	if (val[0])
		{
		printf(" repeat =  %s\n",val);
		norm_proc.ProcessCommand("repeat",val);
		}
	op_ima_obj_attr_get_str(my_id,"segment",128,val);	// server segment size
	if (val[0])
		{
		printf(" segment =  %s\n",val);
		norm_proc.ProcessCommand("segment",val);
		}
	op_ima_obj_attr_get_str(my_id,"block",128,val);		// server blocking size
	if (val[0])
		{
		printf(" block =  %s\n",val);
		norm_proc.ProcessCommand("block",val);
		}
	op_ima_obj_attr_get_str(my_id,"parity",128,val);	// server parity segments calculated per block
	if (val[0])
		{
		printf(" parity =  %s\n",val);
		norm_proc.ProcessCommand("parity",val);
		}
	op_ima_obj_attr_get_str(my_id,"auto",128,val);		// server auto parity count
	if (val[0])
		{
		printf(" auto =  %s\n",val);
		norm_proc.ProcessCommand("auto",val);
		}
	op_ima_obj_attr_get_str(my_id,"extra",128,val);		// server extra parity count
	if (val[0])
		{
		printf(" extra =  %s\n",val);
		norm_proc.ProcessCommand("extra",val);
		}
	op_ima_obj_attr_get_str(my_id,"gsize",128,val);		// group size estimate
	if (val[0])
		{
		printf(" gsize =  %s\n",val);
		norm_proc.ProcessCommand("gsize",val);
		}
	op_ima_obj_attr_get_str(my_id,"grtt",128,val);		// grtt estimate
	if (val[0])
		{
		printf(" grtt =  %s\n",val);
		norm_proc.ProcessCommand("grtt",val);
		}
	op_ima_obj_attr_get_str(my_id,"txbuffer",128,val);	// tx buffer size (bytes)
	if (val[0])
		{
		printf(" txbuffer =  %s\n",val);
		norm_proc.ProcessCommand("txbuffer",val);
		}
	op_ima_obj_attr_get_str(my_id,"rxbuffer",128,val);	// rx buffer size (bytes)
	if (val[0])
		{
		printf(" rxbuffer =  %s\n",val);
		norm_proc.ProcessCommand("rxbuffer",val);
		}
	op_ima_obj_attr_get_str(my_id,"sendRandomFile",128,val);	// queue random-size file size range <sizeMin>:<sizeMax>
	if (val[0])
		{
		printf(" sendRandomFile =  %s\n",val);
		norm_proc.ProcessCommand("sendRandomFile",val);
		}
	op_ima_obj_attr_get_str(my_id,"push",128,val);	// "on" means real-time push stream advancement (non-blocking) on|off
	if (val[0])
		{
		printf(" push =  %s\n",val);
		norm_proc.ProcessCommand("push",val);
		}
	op_ima_obj_attr_get_str(my_id,"flush",128,val);	// stream flush mode (none|passive|active)
	if (val[0])
		{
		printf(" flush =  %s\n",val);
		norm_proc.ProcessCommand("flush",val);
		}
	op_ima_obj_attr_get_str(my_id,"unicastNacks",128,val);	// clients will unicast feedback
	if (val[0])
		{
		printf(" unicastNacks =  %s\n",val);
		norm_proc.ProcessCommand("unicastNacks",val);
		}
	op_ima_obj_attr_get_str(my_id,"silentClient",128,val);	// clients will not transmit
	if (val[0])
		{
		printf(" silentClient =  %s\n",val);
		norm_proc.ProcessCommand("silentClient",val);
		}
	
	norm_conf_udp ();

	FOUT;
	}


void norm_stop()
	{
	norm_proc.OnShutdown();
	}


void
norm_fatal_error (char *emsg)
	{
	char info[40];
	
	/** Abort the simulation with the given message. **/
	FIN (norm_fatal_error (emsg));
   
	sprintf(info, "NORM Error(%s):", node_name);
	op_sim_end (info, emsg, OPC_NIL, OPC_NIL);

	FOUT;
	}


void
norm_warn_error (char *wmsg)
	{
	char info[40];
	
	/** Issue an warning (used for potentially inconsistent situations). **/
	FIN (norm_warn_error (wmsg));
	
	sprintf(info, "NORM Warning(%s):", node_name);

	if (op_prg_odb_ltrace_active ("norm warn"))
		op_prg_odb_print_minor (info, wmsg, OPC_NIL);

	FOUT;
	}



/*  Function to configure the proper input/output streams to the udp process 
    and setup the udp port numbers via the ICI facility  */
void
norm_conf_udp()
	{
	int					outstrm_count;
	Objid				outstrm_objid;
    List*			 	proc_record_handle_list_ptr;
    OmsT_Pr_Handle		temp_process_record_handle;

	FIN(norm_conf_udp());
	
	/*                                                         */
	/* Get the outgoing packet stream index to the UDP process */
	/*                                                         */
	
    /* First, get the number of output streams for the NORM process. */
    outstrm_count = op_topo_assoc_count (my_id, OPC_TOPO_ASSOC_OUT, OPC_OBJTYPE_STRM);
    /* Then, make sure there's two outgoing streams from NORM. */
    if (outstrm_count != 2)
    	norm_fatal_error ("NORM does not have two outgoing streams - one to MGEN & one to UDP.");
	/* Then, get the outgoing stream Objid */
    outstrm_objid = op_topo_assoc (my_id, OPC_TOPO_ASSOC_OUT, OPC_OBJTYPE_STRM, 0);
	/* Retrieve the index of the stream. */
	op_ima_obj_attr_get (outstrm_objid, "src stream", &udp_outstream_index);

    /* 						                                                    */
    /* Locate the UDP module. It must have registered in the process registry.  */
    /* 						                                                    */
	
    /* Obtain the process handles by matching the specific descriptors			*/
    proc_record_handle_list_ptr = op_prg_list_create ();
    oms_pr_process_discover (OPC_OBJID_INVALID, proc_record_handle_list_ptr,
    	"node objid",	OMSC_PR_OBJID,		my_node_id,
    	"protocol",		OMSC_PR_STRING,		"udp",
    	OPC_NIL);

    /* Each node can have only one UDP module.	*/
    if (op_prg_list_size (proc_record_handle_list_ptr) != 1)
    	{
    	/* Having more than one UDP module is a serious error.  End simulation. */
	    norm_fatal_error ("Error: either zero or several UDP processes found in the local node"); 
    	}
    temp_process_record_handle = (OmsT_Pr_Handle) op_prg_list_access (proc_record_handle_list_ptr, OPC_LISTPOS_TAIL);

    /* Obtain the module objid of the udp module	*/
    oms_pr_attr_get (temp_process_record_handle, "module objid",OMSC_PR_OBJID, &my_udp_id);
	
	norm_proc.SetUdpProcessId(my_udp_id);  /*  - OpnetProtoSimProcess.h */

    /* Deallocate the memory allocated for holding the record handle	*/
    while (op_prg_list_size (proc_record_handle_list_ptr) > 0)
    	op_prg_list_remove (proc_record_handle_list_ptr, OPC_LISTPOS_HEAD);

    /* Deallocate the temporary list pointer. */
    op_prg_mem_free (proc_record_handle_list_ptr);	
		
	FOUT;
	}


void norm_host_addr ()
	{
	FIN (norm_host_addr());

    /* Obtain a pointer to the process record handle list of any 
       IP processes residing in the local node.
	*/
    List* proc_record_handle_list_ptr = op_prg_list_create();
    oms_pr_process_discover(OPC_OBJID_INVALID, proc_record_handle_list_ptr,
	                        "protocol", OMSC_PR_STRING, "ip",
	                        "node objid", OMSC_PR_OBJID, my_node_id, 
	                        OPC_NIL);

    /* An error should be created if there are zero or more than     
       one IP processes in the local node.
	*/
    int record_handle_list_size = op_prg_list_size (proc_record_handle_list_ptr);
    IpT_Info* ip_info_ptr;
    if (1 != record_handle_list_size)
		{
	    /* Generate an error and end simulation. */
	    op_sim_end("Error: either zero or more than one ip processes in local node.", "", "", "");
		}
    else
		{
        /* Obtain the process record handle of the IP process. */
	    OmsT_Pr_Handle process_record_handle = (OmsT_Pr_Handle) op_prg_list_access(proc_record_handle_list_ptr, OPC_LISTPOS_HEAD);
	    /* Obtain the pointer to the interface info structure. */
	    oms_pr_attr_get(process_record_handle, "interface information", OMSC_PR_ADDRESS, &ip_info_ptr);
		}

    /* Deallocate the list pointer. */
    while (op_prg_list_size(proc_record_handle_list_ptr) > 0)
	    op_prg_list_remove(proc_record_handle_list_ptr, OPC_LISTPOS_HEAD);
    op_prg_mem_free(proc_record_handle_list_ptr);

    /* Obtain the pointer to the IP interface table.
	   Note that the ip_info_ptr->ip_iface_table_ptr is the same list
	     as module_data->interface_table_ptr as shown in ip_dispatch.pr.c->ip_dispatch_do_int()
	*/
    List* ip_iface_table_ptr = ip_info_ptr->ip_iface_table_ptr;

    /* Obtain the size of the IP interface table. */
    int ip_iface_table_size = op_prg_list_size(ip_iface_table_ptr);

	int interface_info_index = 0;  /* LP */
	
    /* For now, an error should be created if there are zero or more than 
       one IP interface attached to this node.  Loopback interfaces
	   and Tunnel interfaces are OK.
	*/
	
	/* In the future, we should allow more than 1 IP interface for the
	   case of IP routers. LP 3-4-04
	*/
	
    if (1 != ip_iface_table_size)
		{

		/* check to see if there is any loopback interface or tunnel interface.  (LP 3-1-04 - added) */
		int i, ip_intf_count = 0;
		bool dumb_intf = OPC_FALSE;
		for (i = 0; i < ip_iface_table_size; i++)
			{
   			IpT_Interface_Info* intf_ptr = (IpT_Interface_Info*) op_prg_list_access(ip_iface_table_ptr, OPC_LISTPOS_HEAD + i);
			if ((intf_ptr->phys_intf_info_ptr->intf_status == IpC_Intf_Status_Tunnel) ||
				(intf_ptr->phys_intf_info_ptr->intf_status == IpC_Intf_Status_Loopback))
				{
				dumb_intf = OPC_TRUE;
				break;
				} /* end if tunnel || loop back */
			else
				{
				interface_info_index = i;
				ip_intf_count ++;
				}
			} /* end for i */

	    /* Generate an error and end simulation. */
	    if ((dumb_intf == OPC_FALSE) || (ip_intf_count > 1))  /* end LP */
			op_sim_end("Error: either zero or more than one ip interface on this node.", "", "", "");
		}  /* end if ip_iface_table-size != 1 */
	
    	
	/* Obtain a pointer to the IP interface data structure. */	
	IpT_Interface_Info* interface_info_pnt = (IpT_Interface_Info*) op_prg_list_access(ip_iface_table_ptr, OPC_LISTPOS_HEAD + interface_info_index);
	my_ip_addr = interface_info_pnt->addr_range_ptr->address;
	my_ip_mask = interface_info_pnt->addr_range_ptr->subnet_mask;
	host_ipv4_addr.SimSetAddress(my_ip_addr);
//	norm_proc.SetHostAddress(host_ipv4_addr);
		
	FOUT;
	}

OpnetNormProcess::OpnetNormProcess()
 : NormSimAgent(GetTimerMgr(), GetSocketNotifier())
{
 printf("OpnetNormProcess::OpnetNormProcess():  this = %x\n", this);
}  

OpnetNormProcess::~OpnetNormProcess()
{    
}


IpT_Address OpnetNormProcess::addr()
	{
	return my_ip_addr;
	}


bool OpnetNormProcess::OnStartup(int argc, const char*const* argv)
{
	char val[128];
	norm_host_addr();
	op_ima_obj_attr_get_str(my_id,"start",128,val);	// clients will unicast feedback
	if (val[0])
		{
		printf(" NORM start =  %s\n",val);
		ProcessCommand("start",val);
		}
	op_ima_obj_attr_get_str(my_id,"sendStream",128,val);	// send a simulated NORM stream
	if (val[0])
		{
		printf(" NORM sendStream =  %s\n",val);
		ProcessCommand("sendStream",val);
		}
	op_ima_obj_attr_get_str(my_id,"openStream",128,val);	// open a stream object for messaging
	if (val[0])
		{
		printf(" NORM openStream =  %s\n",val);
		ProcessCommand("openStream",val);
		/* JPH 4/11/06  fake attachment of mgen instance */
		AttachMgen((Mgen*)true);
		}
	op_ima_obj_attr_get_str(my_id,"sendFile",128,val);	// queue a "sim" file of <size> bytes for transmission
	if (val[0])
		{
		printf(" sendFile =  %s\n",val);
		ProcessCommand("sendFile",val);
		}
	return true;
} // end OpnetNormProcess::OnStartup()

void OpnetNormProcess::OnShutdown()
{
	ProcessCommand("stop",NULL);
}  // end OpnetNormProcess::OnShutdown()

bool OpnetNormProcess::ProcessCommands(int argc, const char*const* argv) 
{   
    // Process commands, passing unknown commands to base Agent class
    int i = 1;
    while (i < argc)
    {
        NormSimAgent::CmdType cmdType = CommandType(argv[i]);
        switch (cmdType)
        {
            case NormSimAgent::CMD_NOARG:
                if (!ProcessCommand(argv[i], NULL))
                {
                    DMSG(0, "OpnetNormProcess::ProcessCommands() ProcessCommand(%s) error\n", 
                            argv[i]);
                    return false;
                }
                i++;
                break;
                
            case NormSimAgent::CMD_ARG:
                if (!ProcessCommand(argv[i], argv[i+1]))
                {
                    DMSG(0, "OpnetNormProcess::ProcessCommands() ProcessCommand(%s, %s) error\n", 
                            argv[i], argv[i+1]);
                    return false;
                }
                i += 2;
                break;
                
            case NormSimAgent::CMD_INVALID:
                return false;
        }
    }
    return true; 
}  // end OpnetNormProcess::ProcessCommands()


/* This function not used at present.  */
/* SendMessage called directly from send_msg state. */
// JPH 5/25/06 - revert from mgen 42b8 to 42b6
//bool OpnetNormProcess::SendMgenMessage(MgenMsg& theMsg,
//				     bool checksum_enable,
//				     char* txBuffer)
//{
//	int len = 0;
//    return SendMessage(len, txBuffer);
//}  // end OpnetNormProcess::SendMgenMessage()
bool OpnetNormProcess::SendMgenMessage(const char*           txBuffer,
                                  unsigned int          len,
                                  const ProtoAddress&   /*dstAddr*/)
{
    return SendMessage(len, txBuffer);
}  // end OpnetNormProcess::SendMgenMessage()
 


void OpnetNormProcess::ReceivePacketMonitor(Ici* iciptr, Packet* pkptr)
	{

	/* Caclulate metrics to be updated.		*/
	double pk_size = (double) op_pk_total_size_get (pkptr);
	double ete_delay = op_sim_time () - op_pk_creation_time_get (pkptr);

	/* Update local statistics.				*/
	op_stat_write (bits_rcvd_stathandle, 		pk_size);
	op_stat_write (pkts_rcvd_stathandle, 		1.0);
	op_stat_write (ete_delay_stathandle, 		ete_delay);

	op_stat_write (bitssec_rcvd_stathandle, 	pk_size);
	op_stat_write (bitssec_rcvd_stathandle, 	0.0);
	op_stat_write (pktssec_rcvd_stathandle, 	1.0);
	op_stat_write (pktssec_rcvd_stathandle, 	0.0);

	/* Update global statistics.	*/
	op_stat_write (bits_rcvd_gstathandle, 		pk_size);
	op_stat_write (pkts_rcvd_gstathandle, 		1.0);
	op_stat_write (ete_delay_gstathandle, 		ete_delay);

	op_stat_write (bitssec_rcvd_gstathandle, 	pk_size);
	op_stat_write (bitssec_rcvd_gstathandle, 	0.0);
	op_stat_write (pktssec_rcvd_gstathandle, 	1.0);
	op_stat_write (pktssec_rcvd_gstathandle, 	0.0);
	}



void OpnetNormProcess::TransmitPacketMonitor(Ici* ici, Packet* pkptr) 
	{
	/* Caclulate metrics to be updated.		*/
	double pk_size = (double) op_pk_total_size_get (pkptr);

	/* Update local statistics.				*/
	op_stat_write (bits_sent_stathandle, 		pk_size);
	op_stat_write (pkts_sent_stathandle, 		1.0);

	op_stat_write (bitssec_sent_stathandle, 	pk_size);
	op_stat_write (bitssec_sent_stathandle, 	0.0);
	op_stat_write (pktssec_sent_stathandle, 	1.0);
	op_stat_write (pktssec_sent_stathandle, 	0.0);

	/* Update global statistics.	*/
	op_stat_write (bits_sent_gstathandle, 		pk_size);
	op_stat_write (pkts_sent_gstathandle, 		1.0);

	op_stat_write (bitssec_sent_gstathandle, 	pk_size);
	op_stat_write (bitssec_sent_gstathandle, 	0.0);
	op_stat_write (pktssec_sent_gstathandle, 	1.0);
	op_stat_write (pktssec_sent_gstathandle, 	0.0);
	}

void NormSimAgent::HandleMgenMessage(char*   buffer, 
                           unsigned int      buflen, 
                           const ProtoAddress& srcAddr)
	{
	Packet* pkt = op_pk_create(buflen*8);
    char* payload = (char*) op_prg_mem_copy_create((void*)buffer, buflen);
    op_pk_fd_set(pkt, 0, OPC_FIELD_TYPE_STRUCT, payload, 0,
                 op_prg_mem_copy_create, op_prg_mem_free, buflen);
	
	
	op_ici_attr_set(app_ici_ptr, "local_port", local_port);
    op_ici_attr_set(app_ici_ptr, "rem_port", srcAddr.GetPort());
    op_ici_attr_set(app_ici_ptr, "rem_addr", srcAddr.SimGetAddress());
    op_ici_attr_set(app_ici_ptr, "src_addr", srcAddr.SimGetAddress());
    op_ici_install(app_ici_ptr);
  	
    op_pk_send_forced(pkt, OUTSTRM_TO_MGEN);

	}

/* End of Function Block */

/* Undefine optional tracing in FIN/FOUT/FRET */
/* The FSM has its own tracing code and the other */
/* functions should not have any tracing.		  */
#undef FIN_TRACING
#define FIN_TRACING

#undef FOUTRET_TRACING
#define FOUTRET_TRACING

/* Undefine shortcuts to state variables because the */
/* following functions are part of the state class */
#undef norm_proc
#undef my_id
#undef my_node_id
#undef my_pro_id
#undef my_udp_id
#undef my_tcp_id
#undef my_mgen_id
#undef my_ip_addr
#undef my_ip_mask
#undef own_prohandle
#undef own_process_record_handle
#undef pid_string
#undef node_name
#undef timer
#undef udpsocket
#undef tcpsocket
#undef udpnotifier
#undef tcpnotifier
#undef host_ipv4_addr
#undef bits_rcvd_stathandle
#undef bitssec_rcvd_flow_stathandle
#undef bitssec_sent_flow_stathandle
#undef bitssec_rcvd_stathandle
#undef pkts_rcvd_stathandle
#undef pktssec_rcvd_flow_stathandle
#undef pktssec_sent_flow_stathandle
#undef pktssec_rcvd_stathandle
#undef bits_sent_stathandle
#undef bitssec_sent_stathandle
#undef pkts_sent_stathandle
#undef pktssec_sent_stathandle
#undef ete_delay_stathandle
#undef ete_delay_flow_stathandle
#undef bits_rcvd_gstathandle
#undef bitssec_rcvd_gstathandle
#undef pkts_rcvd_gstathandle
#undef pktssec_rcvd_gstathandle
#undef bits_sent_gstathandle
#undef bitssec_sent_gstathandle
#undef pkts_sent_gstathandle
#undef pktssec_sent_gstathandle
#undef ete_delay_gstathandle
#undef udp_outstream_index
#undef local_port
#undef dest_ip_addr
#undef script_fp
#undef source
#undef app_ici_ptr

/* Access from C kernel using C linkage */
extern "C"
{
	VosT_Obtype _op_norm_protolib_init (int * init_block_ptr);
	VosT_Address _op_norm_protolib_alloc (VOS_THREAD_INDEX_ARG_COMMA VosT_Obtype, int);
	void norm_protolib (OP_SIM_CONTEXT_ARG_OPT)
		{
		((norm_protolib_state *)(OP_SIM_CONTEXT_PTR->_op_mod_state_ptr))->norm_protolib (OP_SIM_CONTEXT_PTR_OPT);
		}

	void _op_norm_protolib_svar (void *, const char *, void **);

	void _op_norm_protolib_diag (OP_SIM_CONTEXT_ARG_OPT)
		{
		((norm_protolib_state *)(OP_SIM_CONTEXT_PTR->_op_mod_state_ptr))->_op_norm_protolib_diag (OP_SIM_CONTEXT_PTR_OPT);
		}

	void _op_norm_protolib_terminate (OP_SIM_CONTEXT_ARG_OPT)
		{
		/* The destructor is the Termination Block */
		delete (norm_protolib_state *)(OP_SIM_CONTEXT_PTR->_op_mod_state_ptr);
		}


	VosT_Obtype Vos_Define_Object_Prstate (const char * _op_name, unsigned int _op_size);
	VosT_Address Vos_Alloc_Object_MT (VOS_THREAD_INDEX_ARG_COMMA VosT_Obtype _op_ob_hndl);
	VosT_Fun_Status Vos_Poolmem_Dealloc_MT (VOS_THREAD_INDEX_ARG_COMMA VosT_Address _op_ob_ptr);
} /* end of 'extern "C"' */




/* Process model interrupt handling procedure */


void
norm_protolib_state::norm_protolib (OP_SIM_CONTEXT_ARG_OPT)
	{
#if !defined (VOSD_NO_FIN)
	int _op_block_origin = 0;
#endif
	FIN_MT (norm_protolib_state::norm_protolib ());
	try
		{
		/* Temporary Variables */
		Packet*			data_pkptr;
		Ici*			ici_ptr;
		int				intrpt_type;
		int				intrpt_strm;
		int				intrpt_code;
		
		int				pk_size;
		InetT_Address*	rem_addr_ptr;
		InetT_Address*	intf_addr_ptr;
		InetT_Address	temp_ip_addr;
		InetT_Address	intf_addr;
		int				type_of_service; 
		int				inet_address_supported;
		IpT_Address		rem_ipv4_addr;
		IpT_Address		ipv4_intf_addr;
		UdpT_Port		rem_port;
		int				local_minor_port;
		int				conn_class;
		int				intf_num;
		
		
		
		
		/* End of Temporary Variables */


		FSM_ENTER ("norm_protolib")

		FSM_BLOCK_SWITCH
			{
			/*---------------------------------------------------------*/
			/** state (idle) enter executives **/
			FSM_STATE_ENTER_UNFORCED (0, "idle", state0_enter_exec, "norm_protolib [idle enter execs]")

			/** blocking after enter executives of unforced state. **/
			FSM_EXIT (1,"norm_protolib")


			/** state (idle) exit executives **/
			FSM_STATE_EXIT_UNFORCED (0, "idle", "norm_protolib [idle exit execs]")
				FSM_PROFILE_SECTION_IN ("norm_protolib [idle exit execs]", state0_exit_exec)
				{
				intrpt_type = op_intrpt_type ();
				if (intrpt_type == OPC_INTRPT_STRM)
					intrpt_strm = op_intrpt_strm ();
				else
					intrpt_code = op_intrpt_code ();
				ici_ptr = op_intrpt_ici ();
				}
				FSM_PROFILE_SECTION_OUT (state0_exit_exec)


			/** state (idle) transition processing **/
			FSM_PROFILE_SECTION_IN ("norm_protolib [idle trans conditions]", state0_trans_conds)
			FSM_INIT_COND (END_SIM)
			FSM_TEST_COND (MSG_FROM_LOWER_LAYER)
			FSM_TEST_COND (TIMEOUT_EVENT)
			FSM_TEST_COND (MSG_FROM_HIGHER_LAYER)
			FSM_DFLT_COND
			FSM_TEST_LOGIC ("idle")
			FSM_PROFILE_SECTION_OUT (state0_trans_conds)

			FSM_TRANSIT_SWITCH
				{
				FSM_CASE_TRANSIT (0, 0, state0_enter_exec, norm_stop();, "END_SIM", "norm_stop()", "idle", "idle", "norm_protolib [idle -> idle : END_SIM / norm_stop()]")
				FSM_CASE_TRANSIT (1, 3, state3_enter_exec, ;, "MSG_FROM_LOWER_LAYER", "", "idle", "proc_msg", "norm_protolib [idle -> proc_msg : MSG_FROM_LOWER_LAYER / ]")
				FSM_CASE_TRANSIT (2, 4, state4_enter_exec, ;, "TIMEOUT_EVENT", "", "idle", "itimer", "norm_protolib [idle -> itimer : TIMEOUT_EVENT / ]")
				FSM_CASE_TRANSIT (3, 5, state5_enter_exec, ;, "MSG_FROM_HIGHER_LAYER", "", "idle", "send_msg", "norm_protolib [idle -> send_msg : MSG_FROM_HIGHER_LAYER / ]")
				FSM_CASE_TRANSIT (4, 0, state0_enter_exec, ;, "default", "", "idle", "idle", "norm_protolib [idle -> idle : default / ]")
				}
				/*---------------------------------------------------------*/



			/** state (init) enter executives **/
			FSM_STATE_ENTER_UNFORCED_NOLABEL (1, "init", "norm_protolib [init enter execs]")
				FSM_PROFILE_SECTION_IN ("norm_protolib [init enter execs]", state1_enter_exec)
				{
				app_ici_ptr = op_ici_create ("sink_command");
				
				/* Obtain the object ID of the surrounding norm processor. 	*/
				my_id = op_id_self ();
				
				/* Also obtain the object ID of the surrounding node.		*/
				my_node_id = op_topo_parent (my_id);
				
				/* Obtain the prohandle for this process.					*/
				own_prohandle = op_pro_self ();
				
				/**	Register the process in the model-wide registry.				**/
				own_process_record_handle = (OmsT_Pr_Handle) oms_pr_process_register 
					(my_node_id, my_id, own_prohandle, "NORM");
				
				/*	Register the protocol attribute in the registry. No other	*/
				/*	process should use the string "norm" as the value for its	*/
				/*	"protocol" attribute!										*/
				oms_pr_attr_set (own_process_record_handle, 
					"protocol", 	OMSC_PR_STRING, 	"norm",
					OPC_NIL);
				
				/*	Initialize the state variable used to keep track of the	*/
				/*	NORM module object ID and to generate trace/debugging 	*/
				/*	string information. Obtain process ID of this process. 	*/
				my_pro_id = op_pro_id (op_pro_self ());
				
				/* 	Set the process ID string, to be later used for trace	*/
				/*	and debugging information.								*/
				sprintf (pid_string, "NORM PID (%d)", my_pro_id);
				
				/* Get the name of the surrounding node object */
				op_ima_obj_attr_get (my_node_id, "name", &node_name);
				
				/* 	Schedule a self interrupt to allow  additional initialization. */
				op_intrpt_schedule_self (op_sim_time (), 0);
				}
				FSM_PROFILE_SECTION_OUT (state1_enter_exec)

			/** blocking after enter executives of unforced state. **/
			FSM_EXIT (3,"norm_protolib")


			/** state (init) exit executives **/
			FSM_STATE_EXIT_UNFORCED (1, "init", "norm_protolib [init exit execs]")
				FSM_PROFILE_SECTION_IN ("norm_protolib [init exit execs]", state1_exit_exec)
				{
				norm_init();
				op_intrpt_schedule_self (0.0,0);
				}
				FSM_PROFILE_SECTION_OUT (state1_exit_exec)


			/** state (init) transition processing **/
			FSM_TRANSIT_FORCE (2, state2_enter_exec, ;, "default", "", "init", "init2", "norm_protolib [init -> init2 : default / ]")
				/*---------------------------------------------------------*/



			/** state (init2) enter executives **/
			FSM_STATE_ENTER_UNFORCED (2, "init2", state2_enter_exec, "norm_protolib [init2 enter execs]")

			/** blocking after enter executives of unforced state. **/
			FSM_EXIT (5,"norm_protolib")


			/** state (init2) exit executives **/
			FSM_STATE_EXIT_UNFORCED (2, "init2", "norm_protolib [init2 exit execs]")
				FSM_PROFILE_SECTION_IN ("norm_protolib [init2 exit execs]", state2_exit_exec)
				{
				norm_proc.OnStartup(0,NULL);
				}
				FSM_PROFILE_SECTION_OUT (state2_exit_exec)


			/** state (init2) transition processing **/
			FSM_TRANSIT_FORCE (0, state0_enter_exec, ;, "default", "", "init2", "idle", "norm_protolib [init2 -> idle : default / ]")
				/*---------------------------------------------------------*/



			/** state (proc_msg) enter executives **/
			FSM_STATE_ENTER_FORCED (3, "proc_msg", state3_enter_exec, "norm_protolib [proc_msg enter execs]")
				FSM_PROFILE_SECTION_IN ("norm_protolib [proc_msg enter execs]", state3_enter_exec)
				{
				norm_proc.OnReceive(op_intrpt_strm());
				}
				FSM_PROFILE_SECTION_OUT (state3_enter_exec)

			/** state (proc_msg) exit executives **/
			FSM_STATE_EXIT_FORCED (3, "proc_msg", "norm_protolib [proc_msg exit execs]")


			/** state (proc_msg) transition processing **/
			FSM_TRANSIT_FORCE (0, state0_enter_exec, ;, "default", "", "proc_msg", "idle", "norm_protolib [proc_msg -> idle : default / ]")
				/*---------------------------------------------------------*/



			/** state (itimer) enter executives **/
			FSM_STATE_ENTER_FORCED (4, "itimer", state4_enter_exec, "norm_protolib [itimer enter execs]")
				FSM_PROFILE_SECTION_IN ("norm_protolib [itimer enter execs]", state4_enter_exec)
				{
				norm_proc.OnSystemTimeout();
				}
				FSM_PROFILE_SECTION_OUT (state4_enter_exec)

			/** state (itimer) exit executives **/
			FSM_STATE_EXIT_FORCED (4, "itimer", "norm_protolib [itimer exit execs]")


			/** state (itimer) transition processing **/
			FSM_TRANSIT_FORCE (0, state0_enter_exec, ;, "default", "", "itimer", "idle", "norm_protolib [itimer -> idle : default / ]")
				/*---------------------------------------------------------*/



			/** state (send_msg) enter executives **/
			FSM_STATE_ENTER_FORCED (5, "send_msg", state5_enter_exec, "norm_protolib [send_msg enter execs]")
				FSM_PROFILE_SECTION_IN ("norm_protolib [send_msg enter execs]", state5_enter_exec)
				{
				data_pkptr = op_pk_get (intrpt_strm);
				
				/* The total payload size in bits */
				pk_size = (double) op_pk_total_size_get (data_pkptr);
				
				/* ici processing may have some future use for mgen control of norm */
				/********************************************************************/
				op_ici_attr_get (ici_ptr, "inet_support", &inet_address_supported);
				
				if (inet_address_supported)
					{
					/* This is a udp_command_inet ici.				*/
					op_ici_attr_get (ici_ptr, "rem_addr", &rem_addr_ptr);
				
					op_ici_attr_get (ici_ptr, "src_addr", &intf_addr_ptr);
					}
				else
					{
					/* This is a udp_command_v3 ici.				*/
					op_ici_attr_get (ici_ptr, "rem_addr", &rem_ipv4_addr);
				
					/* Convert the address into InetT_Address form.	*/
					temp_ip_addr = inet_address_from_ipv4_address_create (rem_ipv4_addr);
					rem_addr_ptr = &temp_ip_addr;
				
					/* Get the other fields in the ici.				*/
					op_ici_attr_get (ici_ptr, "src_addr", &ipv4_intf_addr);
					intf_addr = inet_address_from_ipv4_address_create_invalid_check (ipv4_intf_addr);
					intf_addr_ptr = &intf_addr;
					}
				 
				op_ici_attr_get (ici_ptr, "rem_port", &rem_port);
				op_ici_attr_get (ici_ptr, "local_port", &local_port);
				op_ici_attr_get (ici_ptr, "local_minor_port", &local_minor_port);
				op_ici_attr_get (ici_ptr, "connection_class", &conn_class);
				op_ici_attr_get (ici_ptr, "Type of Service", &type_of_service);
				
				/* Using the "strm_index" to identify the outgoing IP interface	*/
				/* to allow sending multicast by setting multicast major port	*/
				op_ici_attr_get (ici_ptr, "strm_index", &intf_num);
				/********************************************************************/
				
				char* txBuffer = (char*)op_prg_mem_alloc(pk_size/8);
				op_pk_fd_get (data_pkptr, 0, &txBuffer);
				//int len = pk_size/8;
				//printf("norm_protolib:send_msg:  txBuffer =\n");
				//for (int i = 0; i<len; i+=8)
				//	printf(" %x %x %x %x %x %x %x %x\n",txBuffer[i],txBuffer[i+1],txBuffer[i+2],txBuffer[i+3],txBuffer[i+4],txBuffer[i+5],txBuffer[i+6],txBuffer[i+7]);		
				norm_proc.SendMessage(pk_size/8, txBuffer);
				
				op_pk_destroy (data_pkptr);
				
				
				
				}
				FSM_PROFILE_SECTION_OUT (state5_enter_exec)

			/** state (send_msg) exit executives **/
			FSM_STATE_EXIT_FORCED (5, "send_msg", "norm_protolib [send_msg exit execs]")


			/** state (send_msg) transition processing **/
			FSM_TRANSIT_FORCE (0, state0_enter_exec, ;, "default", "", "send_msg", "idle", "norm_protolib [send_msg -> idle : default / ]")
				/*---------------------------------------------------------*/



			}


		FSM_EXIT (1,"norm_protolib")
		}
	catch (...)
		{
		Vos_Error_Print (VOSC_ERROR_ABORT,
			(const char *)VOSC_NIL,
			"Unhandled C++ exception in process model (norm_protolib)",
			(const char *)VOSC_NIL, (const char *)VOSC_NIL);
		}
	}




void
norm_protolib_state::_op_norm_protolib_diag (OP_SIM_CONTEXT_ARG_OPT)
	{
	/* No Diagnostic Block */
	}

void
norm_protolib_state::operator delete (void* ptr)
	{
	FIN (norm_protolib_state::operator delete (ptr));
	Vos_Poolmem_Dealloc_MT (OP_SIM_CONTEXT_THREAD_INDEX_COMMA ptr);
	FOUT
	}

norm_protolib_state::~norm_protolib_state (void)
	{

	FIN (norm_protolib_state::~norm_protolib_state ())


	/* No Termination Block */


	FOUT
	}


#undef FIN_PREAMBLE_DEC
#undef FIN_PREAMBLE_CODE

#define FIN_PREAMBLE_DEC
#define FIN_PREAMBLE_CODE

void *
norm_protolib_state::operator new (size_t)
#if defined (VOSD_NEW_BAD_ALLOC)
		throw (VOSD_BAD_ALLOC)
#endif
	{
	void * new_ptr;

	FIN_MT (norm_protolib_state::operator new ());

	new_ptr = Vos_Alloc_Object_MT (VOS_THREAD_INDEX_UNKNOWN_COMMA norm_protolib_state::obtype);
#if defined (VOSD_NEW_BAD_ALLOC)
	if (new_ptr == VOSC_NIL) throw VOSD_BAD_ALLOC();
#endif
	FRET (new_ptr)
	}

/* State constructor initializes FSM handling */
/* by setting the initial state to the first */
/* block of code to enter. */

norm_protolib_state::norm_protolib_state (void) :
		_op_current_block (2)
	{
#if defined (OPD_ALLOW_ODB)
		_op_current_state = "norm_protolib [init enter execs]";
#endif
	}

VosT_Obtype
_op_norm_protolib_init (int * init_block_ptr)
	{
	FIN_MT (_op_norm_protolib_init (init_block_ptr))

	norm_protolib_state::obtype = Vos_Define_Object_Prstate ("proc state vars (norm_protolib)",
		sizeof (norm_protolib_state));
	*init_block_ptr = 2;

	FRET (norm_protolib_state::obtype)
	}

VosT_Address
_op_norm_protolib_alloc (VOS_THREAD_INDEX_ARG_COMMA VosT_Obtype, int)
	{
#if !defined (VOSD_NO_FIN)
	int _op_block_origin = 0;
#endif
	norm_protolib_state * ptr;
	FIN_MT (_op_norm_protolib_alloc ())

	/* New instance will have FSM handling initialized */
#if defined (VOSD_NEW_BAD_ALLOC)
	try {
		ptr = new norm_protolib_state;
	} catch (const VOSD_BAD_ALLOC &) {
		ptr = VOSC_NIL;
	}
#else
	ptr = new norm_protolib_state;
#endif
	FRET ((VosT_Address)ptr)
	}



void
_op_norm_protolib_svar (void * gen_ptr, const char * var_name, void ** var_p_ptr)
	{
	norm_protolib_state		*prs_ptr;

	FIN_MT (_op_norm_protolib_svar (gen_ptr, var_name, var_p_ptr))

	if (var_name == OPC_NIL)
		{
		*var_p_ptr = (void *)OPC_NIL;
		FOUT
		}
	prs_ptr = (norm_protolib_state *)gen_ptr;

	if (strcmp ("norm_proc" , var_name) == 0)
		{
		*var_p_ptr = (void *) (&prs_ptr->norm_proc);
		FOUT
		}
	if (strcmp ("my_id" , var_name) == 0)
		{
		*var_p_ptr = (void *) (&prs_ptr->my_id);
		FOUT
		}
	if (strcmp ("my_node_id" , var_name) == 0)
		{
		*var_p_ptr = (void *) (&prs_ptr->my_node_id);
		FOUT
		}
	if (strcmp ("my_pro_id" , var_name) == 0)
		{
		*var_p_ptr = (void *) (&prs_ptr->my_pro_id);
		FOUT
		}
	if (strcmp ("my_udp_id" , var_name) == 0)
		{
		*var_p_ptr = (void *) (&prs_ptr->my_udp_id);
		FOUT
		}
	if (strcmp ("my_tcp_id" , var_name) == 0)
		{
		*var_p_ptr = (void *) (&prs_ptr->my_tcp_id);
		FOUT
		}
	if (strcmp ("my_mgen_id" , var_name) == 0)
		{
		*var_p_ptr = (void *) (&prs_ptr->my_mgen_id);
		FOUT
		}
	if (strcmp ("my_ip_addr" , var_name) == 0)
		{
		*var_p_ptr = (void *) (&prs_ptr->my_ip_addr);
		FOUT
		}
	if (strcmp ("my_ip_mask" , var_name) == 0)
		{
		*var_p_ptr = (void *) (&prs_ptr->my_ip_mask);
		FOUT
		}
	if (strcmp ("own_prohandle" , var_name) == 0)
		{
		*var_p_ptr = (void *) (&prs_ptr->own_prohandle);
		FOUT
		}
	if (strcmp ("own_process_record_handle" , var_name) == 0)
		{
		*var_p_ptr = (void *) (&prs_ptr->own_process_record_handle);
		FOUT
		}
	if (strcmp ("pid_string" , var_name) == 0)
		{
		*var_p_ptr = (void *) (prs_ptr->pid_string);
		FOUT
		}
	if (strcmp ("node_name" , var_name) == 0)
		{
		*var_p_ptr = (void *) (prs_ptr->node_name);
		FOUT
		}
	if (strcmp ("timer" , var_name) == 0)
		{
		*var_p_ptr = (void *) (&prs_ptr->timer);
		FOUT
		}
	if (strcmp ("udpsocket" , var_name) == 0)
		{
		*var_p_ptr = (void *) (&prs_ptr->udpsocket);
		FOUT
		}
	if (strcmp ("tcpsocket" , var_name) == 0)
		{
		*var_p_ptr = (void *) (&prs_ptr->tcpsocket);
		FOUT
		}
	if (strcmp ("udpnotifier" , var_name) == 0)
		{
		*var_p_ptr = (void *) (&prs_ptr->udpnotifier);
		FOUT
		}
	if (strcmp ("tcpnotifier" , var_name) == 0)
		{
		*var_p_ptr = (void *) (&prs_ptr->tcpnotifier);
		FOUT
		}
	if (strcmp ("host_ipv4_addr" , var_name) == 0)
		{
		*var_p_ptr = (void *) (&prs_ptr->host_ipv4_addr);
		FOUT
		}
	if (strcmp ("bits_rcvd_stathandle" , var_name) == 0)
		{
		*var_p_ptr = (void *) (&prs_ptr->bits_rcvd_stathandle);
		FOUT
		}
	if (strcmp ("bitssec_rcvd_flow_stathandle" , var_name) == 0)
		{
		*var_p_ptr = (void *) (prs_ptr->bitssec_rcvd_flow_stathandle);
		FOUT
		}
	if (strcmp ("bitssec_sent_flow_stathandle" , var_name) == 0)
		{
		*var_p_ptr = (void *) (prs_ptr->bitssec_sent_flow_stathandle);
		FOUT
		}
	if (strcmp ("bitssec_rcvd_stathandle" , var_name) == 0)
		{
		*var_p_ptr = (void *) (&prs_ptr->bitssec_rcvd_stathandle);
		FOUT
		}
	if (strcmp ("pkts_rcvd_stathandle" , var_name) == 0)
		{
		*var_p_ptr = (void *) (&prs_ptr->pkts_rcvd_stathandle);
		FOUT
		}
	if (strcmp ("pktssec_rcvd_flow_stathandle" , var_name) == 0)
		{
		*var_p_ptr = (void *) (prs_ptr->pktssec_rcvd_flow_stathandle);
		FOUT
		}
	if (strcmp ("pktssec_sent_flow_stathandle" , var_name) == 0)
		{
		*var_p_ptr = (void *) (prs_ptr->pktssec_sent_flow_stathandle);
		FOUT
		}
	if (strcmp ("pktssec_rcvd_stathandle" , var_name) == 0)
		{
		*var_p_ptr = (void *) (&prs_ptr->pktssec_rcvd_stathandle);
		FOUT
		}
	if (strcmp ("bits_sent_stathandle" , var_name) == 0)
		{
		*var_p_ptr = (void *) (&prs_ptr->bits_sent_stathandle);
		FOUT
		}
	if (strcmp ("bitssec_sent_stathandle" , var_name) == 0)
		{
		*var_p_ptr = (void *) (&prs_ptr->bitssec_sent_stathandle);
		FOUT
		}
	if (strcmp ("pkts_sent_stathandle" , var_name) == 0)
		{
		*var_p_ptr = (void *) (&prs_ptr->pkts_sent_stathandle);
		FOUT
		}
	if (strcmp ("pktssec_sent_stathandle" , var_name) == 0)
		{
		*var_p_ptr = (void *) (&prs_ptr->pktssec_sent_stathandle);
		FOUT
		}
	if (strcmp ("ete_delay_stathandle" , var_name) == 0)
		{
		*var_p_ptr = (void *) (&prs_ptr->ete_delay_stathandle);
		FOUT
		}
	if (strcmp ("ete_delay_flow_stathandle" , var_name) == 0)
		{
		*var_p_ptr = (void *) (prs_ptr->ete_delay_flow_stathandle);
		FOUT
		}
	if (strcmp ("bits_rcvd_gstathandle" , var_name) == 0)
		{
		*var_p_ptr = (void *) (&prs_ptr->bits_rcvd_gstathandle);
		FOUT
		}
	if (strcmp ("bitssec_rcvd_gstathandle" , var_name) == 0)
		{
		*var_p_ptr = (void *) (&prs_ptr->bitssec_rcvd_gstathandle);
		FOUT
		}
	if (strcmp ("pkts_rcvd_gstathandle" , var_name) == 0)
		{
		*var_p_ptr = (void *) (&prs_ptr->pkts_rcvd_gstathandle);
		FOUT
		}
	if (strcmp ("pktssec_rcvd_gstathandle" , var_name) == 0)
		{
		*var_p_ptr = (void *) (&prs_ptr->pktssec_rcvd_gstathandle);
		FOUT
		}
	if (strcmp ("bits_sent_gstathandle" , var_name) == 0)
		{
		*var_p_ptr = (void *) (&prs_ptr->bits_sent_gstathandle);
		FOUT
		}
	if (strcmp ("bitssec_sent_gstathandle" , var_name) == 0)
		{
		*var_p_ptr = (void *) (&prs_ptr->bitssec_sent_gstathandle);
		FOUT
		}
	if (strcmp ("pkts_sent_gstathandle" , var_name) == 0)
		{
		*var_p_ptr = (void *) (&prs_ptr->pkts_sent_gstathandle);
		FOUT
		}
	if (strcmp ("pktssec_sent_gstathandle" , var_name) == 0)
		{
		*var_p_ptr = (void *) (&prs_ptr->pktssec_sent_gstathandle);
		FOUT
		}
	if (strcmp ("ete_delay_gstathandle" , var_name) == 0)
		{
		*var_p_ptr = (void *) (&prs_ptr->ete_delay_gstathandle);
		FOUT
		}
	if (strcmp ("udp_outstream_index" , var_name) == 0)
		{
		*var_p_ptr = (void *) (&prs_ptr->udp_outstream_index);
		FOUT
		}
	if (strcmp ("local_port" , var_name) == 0)
		{
		*var_p_ptr = (void *) (&prs_ptr->local_port);
		FOUT
		}
	if (strcmp ("dest_ip_addr" , var_name) == 0)
		{
		*var_p_ptr = (void *) (&prs_ptr->dest_ip_addr);
		FOUT
		}
	if (strcmp ("script_fp" , var_name) == 0)
		{
		*var_p_ptr = (void *) (&prs_ptr->script_fp);
		FOUT
		}
	if (strcmp ("source" , var_name) == 0)
		{
		*var_p_ptr = (void *) (&prs_ptr->source);
		FOUT
		}
	if (strcmp ("app_ici_ptr" , var_name) == 0)
		{
		*var_p_ptr = (void *) (&prs_ptr->app_ici_ptr);
		FOUT
		}
	*var_p_ptr = (void *)OPC_NIL;

	FOUT
	}

