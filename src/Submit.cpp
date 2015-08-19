#include <cstdlib> // atoi
#include "Submit.h"
#include "Messages.h"
#include "StringRoutines.h"

Submit::~Submit() {
  if (Run_ != 0) delete Run_;
  if (Analyze_ != 0) delete Analyze_;
  if (Archive_ != 0) delete Archive_;
}

int Submit::SubmitRuns(std::string const& top, StrArray const& RunDirs, int start) const {
  Run_->Info();
  Run_->CalcThreads();
  std::string user = UserName();
  Msg("User: %s\n", user.c_str());
  std::string submitScript( std::string(Run_->SubmitCmd()) + ".sh" );
  // Set RUNTYPE-specific command line options
  std::string groupfileName("groupfile"); // TODO make these filenames options
  std::string remddimName("remd.dim");
  std::string cmd_opts;
  if (Run_->RunType() == MREMD)
    cmd_opts.assign("-ng $NG -groupfile " + groupfileName + " -remd-file " + remddimName);
  else if (Run_->RunType() == HREMD )
    cmd_opts.assign("-ng $NG -groupfile " + groupfileName + " -rem 3");
  else if (Run_->RunType() == TREMD )
    cmd_opts.assign("-ng $NG -groupfile " + groupfileName + " -rem 1");
  // Create run script for each run directory
  std::string previous_jobid;
  int run_num;
  if (start != -1)
    run_num = start;
  else
    run_num = 0;
  Msg("Submitting %zu runs.\n", RunDirs.size());
  for (StrArray::const_iterator rdir = RunDirs.begin(); rdir != RunDirs.end(); ++rdir)
  {
    ChangeDir( top );
    // Check if run directories already contain scripts
    if ( !Run_->OverWrite() && fileExists( *rdir + "/" + submitScript) ) {
      ErrorMsg("Not overwriting (-O) and %s already contains %s\n",
               rdir->c_str(), submitScript.c_str());
      if (!Run_->SetupDepend())
        return 1;
      else
        continue;
    }
    Msg("  %s\n", rdir->c_str());
    ChangeDir( *rdir );
    // Run-specific sanity checks. For MREMD make sure groupfile and remd.dim
    // exist. For HREMD/TREMD make sure groupfile exists. For MD groupfile is
    // used to get command line options only.
    if (Run_->RunType() == TREMD || Run_->RunType() == HREMD) {
      if (CheckExists("groupfile", groupfileName)) return 1;
    } else if (Run_->RunType() == MREMD) {
      if (CheckExists("groupfile", groupfileName)) return 1;
      if (CheckExists("remd.dim", "remd.dim")) return 1;
    } else if (Run_->RunType() == MD) {
      if (!fileExists(groupfileName)) {
        ErrorMsg("groupfile does not exist for '%s'\n", rdir->c_str());
        if (!Run_->SetupDepend())
          return 1;
        else
          continue;
      }
      int nlines = 0;
      TextFile groupfile;
      if (groupfile.OpenRead(groupfileName)) return 1;
      const char* ptr = groupfile.Gets();
      while (ptr != 0) {
        nlines++;
        cmd_opts.assign( ptr );
        ptr = groupfile.Gets();
      }
      groupfile.Close();
      if (nlines > 1)
        cmd_opts.assign("-ng $NG -groupfile " + groupfileName);
    }
    // Set options specific to queuing system, node info, and Amber env.
    Run_->QsubHeader(submitScript, run_num, previous_jobid);
  }
  Msg("CmdOpts: %s\n", cmd_opts.c_str());

  return 0; 
}

int Submit::SubmitAnalysis(std::string const& top) {
  if (Analyze_ == 0) {
    ErrorMsg("No ANALYSIS_FILE set.\n");
    return 1;
  }
  Analyze_->SetRunType( ANALYSIS );
  Analyze_->Info();
  return Analyze_->Submit( top );
}

int Submit::ReadOptions(std::string const& fn) {
  if (Run_ != 0) {
    ErrorMsg("Only one queue input allowed.\n");
    return 1;
  }
  Run_ = new QueueOpts();
  if (ReadOptions( fn, *Run_ )) return 1;
  if (Run_->Check()) return 1;
  if (Analyze_ != 0 && Analyze_->Check()) return 1;
  if (Archive_ != 0 && Archive_->Check()) return 1;
  return 0;
}

int Submit::ReadOptions(std::string const& fnameIn, QueueOpts& Qopt) {
  n_input_read_++;
  if (n_input_read_ > 100) {
    ErrorMsg("# of input files read > 100; possible infinite recursion.\n");
    return 1;
  }

  Msg("  Reading queue options from '%s'\n", fnameIn.c_str());
  std::string fname = tildeExpansion( fnameIn );
  if (CheckExists( "Queue options", fname )) return 1;
  TextFile infile;
  if (infile.OpenRead( fname )) return 1;
  const char* ptr = infile.Gets();
  while (ptr != 0) {
    // Format is <NAME> <OPTIONS>
    std::string line( ptr );
    size_t found = line.find_first_of(" ");
    if (found == std::string::npos) {
      ErrorMsg("malformed option: %s\n", ptr);
      return 1;
    }
    size_t found1 = found;
    while (found1 < line.size() && line[found1] == ' ') ++found1;
    std::string Args = line.substr(found1);
    // Remove any newline chars
    found1 = Args.find_first_of("\r\n");
    if (found1 != std::string::npos) Args.resize(found1);
    // Reduce line to option
    line.resize(found);
    //Msg("Opt: '%s'   Args: '%s'\n", line.c_str(), Args.c_str());

    // Process options
    if (line == "ANALYZE_FILE") {
      if (Analyze_ != 0) {
        ErrorMsg("Only one ANALYZE_FILE allowed.\n");
        return 1;
      }
      Analyze_ = new QueueOpts(); // TODO Copy of existing?
      if (ReadOptions( Args, *Analyze_ )) return 1;
    } else if (line == "ARCHIVE_FILE") {
      if (Archive_ != 0) {
        ErrorMsg("Only one ARCHIVE_FILE allowed.\n");
        return 1;
      }
      Archive_ = new QueueOpts();
      if (ReadOptions( Args, *Archive_ )) return 1;
    } else if (line == "INPUT_FILE") {
      // Try to prevent recursion.
      std::string fn = tildeExpansion( Args );
      if (fn == fname) {
        ErrorMsg("An input file may not read from itself (%s).\n", Args.c_str());
        return 1;
      }
      if (ReadOptions( Args, Qopt )) return 1;
    } else {
      if (Qopt.ProcessOption(line, Args)) return 1;
    }
    
    ptr = infile.Gets();
  }
  infile.Close();

  return 0;
}
  

// =============================================================================
Submit::QueueOpts::QueueOpts() :
  nodes_(0),
  ng_(0),
  ppn_(0),
  threads_(0),
  runType_(MD),
  overWrite_(false),
  testing_(false),
  queueType_(PBS),
  isSerial_(false),
  dependType_(BATCH),
  setupDepend_(true)
{}

const char* Submit::QueueOpts::RunTypeStr[] = {
  "MD", "TREMD", "HREMD", "MREMD", "ANALYSIS", "ARCHIVE"
};

const char* Submit::QueueOpts::QueueTypeStr[] = {
  "PBS", "SBATCH"
};

const char* Submit::QueueOpts::SubmitCmdStr[] = {
  "qsub", "sbatch"
};

static inline int RetrieveOpt(const char** Str, int end, std::string const& VAR) {
  for (int i = 0; i != end; i++)
    if ( VAR.compare( Str[i] )==0 )
      return i;
  return end;
}

int Submit::QueueOpts::ProcessOption(std::string const& OPT, std::string const& VAR) {
  //Msg("Processing '%s' '%s'\n", OPT.c_str(), VAR.c_str()); // DEBUG

  if      (OPT == "JOBNAME") job_name_ = VAR;
  else if (OPT == "NODES"  ) nodes_ = atoi( VAR.c_str() );
  else if (OPT == "NG"     ) ng_ = atoi( VAR.c_str() );
  else if (OPT == "PPN"    ) ppn_ = atoi( VAR.c_str() );
  else if (OPT == "THREADS") threads_ = atoi( VAR.c_str() );
  else if (OPT == "RUNTYPE") {
    runType_ = (RUNTYPE)RetrieveOpt(RunTypeStr, NO_RUN, VAR);
    if (runType_ == NO_RUN) {
      ErrorMsg("Unrecognized run type: %s\n", VAR.c_str());
      return 1;
    }
  }
  else if (OPT == "AMBERHOME") {
    if ( CheckExists("AMBERHOME", VAR) ) return 1;
    amberhome_ = tildeExpansion( VAR );
  }
  else if (OPT == "PROGRAM"  ) program_ = VAR;
  else if (OPT == "QSUB"     ) {
    queueType_ = (QUEUETYPE) RetrieveOpt(QueueTypeStr, NO_QUEUE, VAR);
    if (queueType_ == NO_QUEUE) {
      ErrorMsg("Unrecognized QSUB: %s\n", VAR.c_str());
      return 1;
    }
  }
  else if (OPT == "WALLTIME"  ) walltime_ = VAR;
  else if (OPT == "NODEARGS"  ) nodeargs_ = VAR;
  else if (OPT == "MPIRUN"    ) mpirun_ = VAR;
  else if (OPT == "MODULEFILE") {
    if ( CheckExists( "Module file", VAR ) ) return 1;
    TextFile modfile;
    if (modfile.OpenRead( tildeExpansion(VAR) )) return 1;
    const char* ptr = modfile.Gets();
    while (ptr != 0) {
      additionalCommands_.append( std::string(ptr) );
      ptr = modfile.Gets();
    }
    modfile.Close();
  }
  else if (OPT == "ACCOUNT") account_ = VAR;
  else if (OPT == "EMAIL"  ) email_ = VAR;
  else if (OPT == "QUEUE"  ) queueName_ = VAR;
  else if (OPT == "SERIAL" ) isSerial_ = (bool)atoi( VAR.c_str() );
  else if (OPT == "CHAIN"  ) {
    // TODO modify if more chain options introduced
    int ival = atoi( VAR.c_str() );
    if (ival == 1) dependType_ = SUBMIT;
  }
  else if (OPT == "NO_DEPEND") setupDepend_ = !((bool)atoi( VAR.c_str() ));
  else if (OPT == "FLAG"     ) Flags_.push_back( VAR );
  else {
    ErrorMsg("Unrecognized option '%s' in input file.\n", OPT.c_str());
    return 1;
  }
  return 0;
}

int Submit::QueueOpts::Check() const {
  if (job_name_.empty()) {
    ErrorMsg("No job name\n");
    return 1;
  }
  if (program_.empty()) { // TODO check AMBERHOME ?
    ErrorMsg("PROGRAM not specified.\n");
    return 1;
  }
  if (!isSerial_ && mpirun_.empty()) {
    ErrorMsg("MPI run command MPIRUN not set.\n");
    return 1;
  }
  return 0;
}

int Submit::QueueOpts::Submit(std::string const& WorkDir) const {
  // Get user name from whoami command
  std::string user = UserName();
  Msg("User: %s\n", user.c_str());
  return 0;
}

void Submit::QueueOpts::Info() const {
  Msg("\n---=== Job Submission ===---\n");
  Msg("  RUNTYPE   : %s\n", RunTypeStr[runType_]);
  Msg("  JOBNAME   : %s\n", job_name_.c_str());
  if (nodes_ > 0  ) Msg("  NODES     : %i\n", nodes_);
  if (ng_ > 0     ) Msg("  NG        : %i\n", ng_);
  if (ppn_ > 0    ) Msg("  PPN       : %i\n", ppn_);
  if (threads_ > 0) Msg("  THREADS   : %i\n", threads_);
  if (!amberhome_.empty()) Msg("  AMBERHOME : %s\n", amberhome_.c_str());
  Msg("  PROGRAM   : %s\n", program_.c_str());
  Msg("  QSUB      : %s\n", QueueTypeStr[queueType_]);
  if (!walltime_.empty())  Msg("  WALLTIME  : %s\n", walltime_.c_str());
  if (!mpirun_.empty())    Msg("  MPIRUN    : %s\n", mpirun_.c_str());
  if (!nodeargs_.empty())  Msg("  NODEARGS  : %s\n", nodeargs_.c_str());
  if (!account_.empty())   Msg("  ACCOUNT   : %s\n", account_.c_str());
  if (!email_.empty())     Msg("  EMAIL     : %s\n", email_.c_str());
  if (!queueName_.empty()) Msg("  QUEUE     : %s\n", queueName_.c_str());
  Msg("  CHAIN     : %i\n", (int)dependType_);
  Msg("  NO_DEPEND : %i\n", (int)(!setupDepend_));
}

void Submit::QueueOpts::CalcThreads() {
  if (threads_ < 1) threads_ = nodes_ * ppn_;
  if (threads_ < 1)
    Msg("Warning: Less than 1 thread specified.\n");
}

void Submit::QueueOpts::AdditionalFlags(TextFile& qout) const {
  for (Sarray::const_iterator flag = Flags_.begin(); flag != Flags_.end(); ++flag)
    qout.Printf("#%s %s\n", QueueTypeStr[queueType_], flag->c_str());
}

int Submit::QueueOpts::QsubHeader(std::string const& script, int run_num, std::string const& jobID)
{
  if (script.empty()) {
    ErrorMsg("QsubHeader: No script name.\n");
    return 1;
  }
  Msg("Writing %s\n", script.c_str());
  std::string job_title, previous_job;
  if (run_num > -1)
    job_title = job_name_ + "." + integerToString(run_num);
  else
    job_title = job_name_;
  if (dependType_ != SUBMIT)
    previous_job = jobID;
  // Queue specific options.
  TextFile qout;
  if (qout.OpenWrite(script)) return 1;
  if (queueType_ == PBS) {
    std::string resources("nodes=" + integerToString(nodes_));
    if (ppn_ > 0)
      resources.append(":ppn=" + integerToString(ppn_));
    resources.append(nodeargs_);
    qout.Printf("#PBS -S /bin/bash\n#PBS -l walltime=%s,%s\n#PBS -N %s\n#PBS -j oe\n",
                walltime_.c_str(), resources.c_str(), job_title.c_str());
    if (!email_.empty()) qout.Printf("#PBS -m abe\n#PBS -M %s\n", email_.c_str()); 
    if (!account_.empty()) qout.Printf("#PBS -A %s\n", account_.c_str());
    if (!previous_job.empty()) qout.Printf("#PBS -W depend=afterok:%s\n", previous_job.c_str());  
    if (!queueName_.empty()) qout.Printf("#PBS -q %s\n", queueName_.c_str());
    AdditionalFlags( qout );
    qout.Printf("\ncd $PBS_O_WORKDIR\n");
  } 
  
  // Additional Flags and finish.
  //QW->Finish(Flags_);

  qout.Close();
  return 0;
}
