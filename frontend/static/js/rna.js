/*
 * "enums" defined here must be in sync with server side defines
 */
var 	FRONTEND_PROTOCOL='https://',
	FRONTEND_URI='rna.liacs.nl',
	FRONTEND_PORT=undefined,
	FRONTEND_WS_PROTOCOL='wss://',
	FRONTEND_COOKIE_SECURITY_ATTRIBUTES={ SameSite:'lax', secure:true },
	FRONTEND_TOPIC=Object.freeze ({'SEQUENCES':0,'CSSD':1,'JOBS':2}),
	FRONTEND_CAPABILITY=Object.freeze ({'NEW':0,'DELETE':1,'ORDER':2,'SHOW':3,'SEARCH':4,'EDIT':5}),
	FRONTEND_KEY_TOPIC='topic',
	FRONTEND_KEY_CAPABILITY='capability',
	FRONTEND_KEY_STATUS='status',
	FRONTEND_STATUS_SUCCESS='success',
	FRONTEND_KEY_COUNT='count',
	FRONTEND_KEY_TIME='time',
	FRONTEND_URI_PATH=Object.freeze ({'WS':'/websocket','TC':'/tc','CSSD':'/cssd','CSSDS':'/cssds','SEQUENCE':'/sequence','SEQUENCES':'/sequences','JOB':'/job','JOBS':'/jobs','JOB_W_RESULTS':'/job-w-results','RESULTS':'/results','RESULTS_SUMMARY':'/results-summary','RESULT':'/result','RESULT_INDEX':'/result-index'}),
	FRONTEND_WS_URI=Object.freeze ("".concat (FRONTEND_WS_PROTOCOL, FRONTEND_URI, FRONTEND_PORT?":"+FRONTEND_PORT:"", FRONTEND_URI_PATH.WS)),
	FRONTEND_WS_MSG_TYPE=Object.freeze ({'HEARTBEAT':'0', 'CLOSE_REQUEST':'1', 'UPDATE_NOTIFICATION':'2' , 'LIMIT_REACHED':'3', 'ACCESS_TOKEN_REFRESH':'4'}),
	FRONTEND_WS_MSG_ACCESS_TOKEN_REFRESH_DELIMITER=' ',
	FRONTEND_WS_CLOSE_ATTEMPT_TIMEOUT_MS=10,
	FRONTEND_WS_LOST_CONNECTION_TIMEOUT_MS=10000,
	FRONTEND_WS_PENDING_AUTHORIZATION_WAIT_MS=20,
	FRONTEND_WS_PENDING_AUTHORIZATION_TIMEOUT_MS=5000,
	FRONTEND_WS_MAX_RESTART_ATTEMPTS=10,
	FRONTEND_WS_MAX_REOPEN_ATTEMPTS=30,
	FRONTEND_WS_HEARTBEAT_DELAY_MS=10000,			// pause before sending heartbeat to server; must be a delta less than server heartbeat setting...
	FRONTEND_NEWLINE_CHAR='\n',
	FRONTEND_FIELD_SEPARATOR_CHAR='\t',

	FRONTEND_QUERY_WAIT_MS=500,				// give pause before re-checking submission of last query

	FRONTEND_KEY_ERROR_MSG='error', 			// JSON object key used by backend to send back error message values
	FRONTEND_HIGHLIGHT_FLASH_DELAY_MS=70,
	FRONTEND_HIGHLIGHT_FLASH_REPEAT=2,

	FRONTEND_API_MAX_ATTEMPTS=30,				
	FRONTEND_API_ATTEMPT_TIMEOUT_MS=200,

	FRONTEND_TC_MAX_ATTEMPTS=4,				// grace period for frontend to negotiate topics/capabilities with backend;
	FRONTEND_TC_ATTEMPT_TIMEOUT_MS=500,			// must be at least as long as FRONTEND_WS_HEARTBEAT_DELAY_MS

	FRONTEND_HIT_SEEK_MAX_ATTEMPTS=20,
	FRONTEND_HIT_SEEK_MS=100,

	FRONTEND_CR_LOG_LIMIT=500,

	FRONTEND_Y_TOLERANCE=2,					// number of pixels to tolerate when comparing parent/child y+height overlaps

	jobs_api_attempts=FRONTEND_API_MAX_ATTEMPTS,		// API call to update multiple jobs
	job_api_attempts=FRONTEND_API_MAX_ATTEMPTS,		// API call to update single job
	sequences_api_attempts=FRONTEND_API_MAX_ATTEMPTS,
	cssds_api_attempts=FRONTEND_API_MAX_ATTEMPTS,
	results_api_attempts=FRONTEND_API_MAX_ATTEMPTS,
	tc_api_attempts=FRONTEND_API_MAX_ATTEMPTS,

	ACTION=0,

	// mirror DS_COLS_* at API service point
	DS_COLS_SEQUENCES=Object.freeze ({'_ID':0, 'ACCESSION':1, 'DEFINITION':2, '3P_UTR_ALT':3, 'GROUP':4}),
	DS_COLS_CSSD=Object.freeze ({'_ID':0, 'STRING':1, 'NAME':2, 'REF_ID':3, 'PUBLISHED':4}),
	DS_COLS_JOBS=Object.freeze ({'_ID':0, 'STRING':1, 'NAME':2, 'REF_ID':3, 'ACCESSION':4, 'DEFINITION':5,
				     '3P_UTR_ALT':6, 'GROUP':7, 'SEQUENCE_ID':8, 'CSSD_ID':9, 'STATUS':10}),

	FRONTEND_GUEST_TEMPLATE_REF_ID="0",	// map backend guest ref_id to the same ref_id here (otherwise assumed registered user/private)

	DS_JOB_STATUS_DONE='3',
	DS_JOB_STATUS_SUBMITTED='2',
	DS_JOB_STATUS_PENDING='1',
	DS_JOB_STATUS_INIT='0',
	DS_JOB_ERROR_OK='0',
	DS_JOB_ERROR_FAIL='-1',

	DS_COLS_RESULTS=Object.freeze ({'_ID':0, 'TIME':1, 'POSITION':2, 'FE':3, 'STRING':4, 'REF_ID':5}),
	DS_COLS_RESULTS_SUMMARY=Object.freeze ({'POSITION':0,'MIN_FE':1,'MAX_FE':2,'AVG_FE':3,'STD_FE':4,'COUNT':5}),

	DS_OP_TYPE=Object.freeze ({'UNKNOWN':0,'INSERT':1, 'UPDATE':2, 'DELETE':3}),

	FRONTEND_CSSD_STRING_SEPARATOR_HTML_PREFIX='',
	FRONTEND_CSSD_STRING_SEPARATOR_HTML_SUFFIX='<br>',

	FRONTEND_CSSD_NAME_SEPARATOR_HTML_PREFIX='',
	FRONTEND_CSSD_NAME_SEPARATOR_HTML_SUFFIX='',

	FRONTEND_DEFAULT_DS_LIMIT=10,
	FRONTEND_DEFAULT_DS_ORDER="fe",

	FRONTEND_MAX_STRING_DISPLAY_LENGTH=1000,		// right trim row fields, over and above any styling constraints
	FRONTEND_MAX_STRING_DISPLAY_ELLIPSES='...',

	FRONTEND_FLOAT_PRECISION=2,

	FRONTEND_NO_SEQUENCE_BANNER='&nbsp;',
	FRONTEND_NO_CSSD_BANNER='&nbsp;',
	FRONTEND_WHITEPSACE='&nbsp;',

	RNAws=undefined,
	RNAwsSlot=undefined,
	RNAwsReopenAttempts=FRONTEND_WS_MAX_REOPEN_ATTEMPTS,
	RNAwsCloseRequested=false,
	RNAwsLimitExceeded=false,
	RNAkoModel=undefined,

	AUTH0_CLIENT_ID= 		'SV4c3ndIod0mXf3YDQj3PjojGmXQgXVD',
	AUTH0_DOMAIN= 			'rna.eu.auth0.com',
	AUTH0_SCOPE= 			'openid profile',
	AUTH0_GUEST_ACCESS_TOKEN= 	'00', 			// 0-filled string of arbitrary len but > 1 (which is reserved for control messages)
	AUTH0_RESPONSE_TYPE= 		'token',
	AUTH0_CALLBACK_URL= 		"".concat (FRONTEND_PROTOCOL, FRONTEND_URI, FRONTEND_PORT?':'+FRONTEND_PORT:'', '/'),
	AUTH0_LOGO= 			"".concat (FRONTEND_PROTOCOL, FRONTEND_URI, FRONTEND_PORT?':'+FRONTEND_PORT:'', '/img/RNA4.png.gz'),
	AUTH0_PRIMARY_COLOR= 		'#212529',
	AUTH0_AUTH_PARAMS= 		{
						scope: 		AUTH0_SCOPE
	},
	AUTH0_AUTH=			{
						responseType: 	AUTH0_RESPONSE_TYPE
	},
	AUTH0_AUTH_AND_PARAMS=		{
						params:  	AUTH0_AUTH_PARAMS,
						responseType: 	AUTH0_RESPONSE_TYPE
	},
	AUTH0_LOCK_BASE_OPTIONS={
		socialButtonStyle: 	'big',
		allowAutocomplete: 	true,
		allowShowPassword:  	true,
		loginAfterSignUp: 	true,
		closable:  		true,
		autoclose: 		true,
		auth: 			AUTH0_AUTH,
		theme: 			{
						logo: AUTH0_LOGO,
						primaryColor: AUTH0_PRIMARY_COLOR
					},
		languageDictionary: 	{
						title: '',
						emailInputPlaceholder: "your email",
						forgotPasswordAction: "(request login assistance)",
						signUpTitle: '',
						signUpSubmitLabel: 'Register new Access Profile',
						success: {
						    logIn: 'Welcome to RNA'
						},
						error: 	{
							signUp: {
					      				user_exists: 'The profile already exists.'
								}
						}						
					},
		mustAcceptTerms: 	true
	},
	AUTH0_LOCK_LOGIN_OPTIONS=Object.assign ({
							initialScreen: 'login',
							allowLogin: true,
							allowSignUp: false
						}, AUTH0_LOCK_BASE_OPTIONS),
	AUTH0_LOCK_SIGNUP_OPTIONS=Object.assign ({
							initialScreen: 'signUp',
							allowLogin: false,
							allowSignUp: true,
							callbackURL: AUTH0_CALLBACK_URL
						}, AUTH0_LOCK_BASE_OPTIONS),
	AUTH0_LOGOUT_OPTION={returnTo: "".concat (FRONTEND_PROTOCOL, FRONTEND_URI, FRONTEND_PORT?':'+FRONTEND_PORT:'', '/')};

// datastore update notifications received. order by expected frequency of occurrence
let DS_UPDATE_COLLECTION=new Map ([[8, {action: refreshJobsHit_CountTimeStats, name: 'hits', update_delay: 500, last_update: {}}],	// high update delay for results/hits (due to potentially high volume of updates)
				   [4, {action: refreshJobsData, name: 'jobs', update_delay: 100, last_update: {}}],
				   [2, {action: refreshCSSDsData, name: 'cssd', update_delay: 0, last_update: {}}],
                                   [1, {action: refreshSequencesData, name: 'sequences', update_delay: 0, last_update: {}}],
				   [0, {action: function () {}, name: undefined, update_delay: 0, last_update: {}}]]);

/*
 * other customization
 */
var loggerToConsole=false;

/*
 * misc. functions
 */
String.prototype.trunc = String.prototype.trunc || function (n) {
	return (this.length>n)?this.substr (0, n-1)+'...':this;
};

/*
 * knockout model
 */
function koModel () {
	var self=this;

	/*
	 * sequences, CSSD, jobs, hits (results) observable declarations
	 */	 
	self.isShowingModal=false;
	self.isShowingStatsModal=false;
	self.sequences=ko.observableArray ();
	self.sequenceGroups=ko.observableArray ();
	self.selectSequencesScrollStyle=ko.observable ('overflow-y: scroll; scrollbar-width: none;');	// required to sync header and content div scrollbars;
	self.chooseCSSDsScrollStyle=ko.observable ('overflow-y: scroll; scrollbar-width: none;');  	// note: if ResizeObserver is not available then default to always ('scroll')
	self.viewJobsScrollStyle=ko.observable ('overflow-y: scroll; scrollbar-width: none;');
	self.showHitsScrollStyle=ko.observable ('overflow-y: scroll; scrollbar-width: none;');
	self.CSSDs=ko.observableArray ();
	self.jobs=ko.observableArray ();
	self.hits=ko.observableArray ();
	self.hitIndex=ko.observable (0);
	self.hitPage=ko.observable (0);
	self.scrollIntoViewEnabled=true;					// enables/disables scrollIntoView functionality when updating job list
	self.viewedJob=ko.observable (undefined);
	self.viewedJob.subscribe (function () {
		// update hits data each time the currently viewed job changes
		refreshViewedJobHitCount ();
		refreshViewedJobHitData ();
		// same for hit stats
		refreshViewedJobHitStats ();
	});
	self.viewedJobFullSequence=ko.computed (function () {
		var thisJob=self.viewedJob ();
		if (undefined!==thisJob)
		{
			var theSequences=self.sequences ();
			for (var i=0; i<theSequences.length; i++)
			{
				if (thisJob.sequence_id===theSequences[i]._id)
				{
					var fullSequence;

					if (undefined!==theSequences[i].full_nt)
					{
						fullSequence=theSequences[i].full_nt;
					}
					else
					{
						fullSequence=theSequences[i].seqnt;
					}

					return fullSequence;
				}
			}
		}
		return undefined;
	});
	self.viewedJobStatus=ko.computed (function () {
		if (undefined!==self.viewedJob ())
		{
			var nh=viewedJobNumHits ();
			return self.viewedJob().definition.trunc (20)+',  '+
			       self.viewedJob().name.trunc (20)+',  '+
			       self.viewedJob().status()+',  '+
			       nh+(nh!==1?' hits':' hit');
		} 
		else
		{
			return '';
		}
	});
	self.viewedJobFullSequenceLength=ko.computed (function () {
		if (undefined!==self.viewedJobFullSequence())
		{
			return self.viewedJobFullSequence().length;
		}
		else
		{
			return 0;
		}
	});
	viewingJobs=ko.observable (false);
	viewedJobNumHits=ko.observable (-1);
	viewedJobNumHitsMessage=ko.computed (function () {
		if (viewingJobs ())
		{
			var nh=viewedJobNumHits ();
			if (0<nh)
			{
				return nh + (nh > 1 ? ' hits retrieved' : ' hit retrieved');
			}
		}
		return FRONTEND_WHITEPSACE;
	});
	viewedJobNumHitsIndices=ko.computed (function () {
		if (viewingJobs ())
		{
			var nh=viewedJobNumHits ();
			if (0<nh)
			{
				var 	hl=hitLimit (),
					topIndex=1+(hl*self.hitPage ()),
					bottomIndex=Math.min (nh, (topIndex+hl-1));
				if (0<topIndex)
				{
					if (topIndex<bottomIndex)
					{
						return 'viewing '+topIndex+' to '+bottomIndex;
					}
					else
					{
						return 'viewing hit';
					}
				}
			}
		}
		return FRONTEND_WHITEPSACE;
	});
	self.viewedHit=ko.observable (undefined);
	self.numSequencesSelected=ko.observable (0);
	self.sequencesSelected=[];
	self.numCSSDsSelected=ko.observable (0);
	self.CSSDsSelected=[];
	self.jobsSelected=[];
	self.numJobsSelected=ko.observable (0);
	numHitsSelected=ko.observable (0);
	hitLimit=ko.observable (FRONTEND_DEFAULT_DS_LIMIT);
	hitOrder=ko.observable (FRONTEND_DEFAULT_DS_ORDER);
	hitSummaryData=ko.observable (FRONTEND_WHITEPSACE).extend({ deferred: true });
	hitSequence=ko.computed (function () {
		if (viewingJobs ())
		{
			var thisJob=self.viewedJob ();
			if (undefined!==thisJob)
			{
				var theSequences=self.sequences ();
				for (var i=0; i<theSequences.length; i++)
				{
					if (thisJob.sequence_id===theSequences[i]._id)
					{
						var fullSequence, thisHit=self.viewedHit ();

						if (undefined!==theSequences[i].full_nt)
						{
							fullSequence=theSequences[i].full_nt;
						}
						else
						{
							fullSequence=theSequences[i].seqnt;
						}

						if (undefined!==fullSequence && undefined!==thisHit &&
						    undefined!==thisHit.hit && undefined!==thisHit.position &&
						    thisHit.hit.length<=fullSequence.length)
						{
							var thisHitPosition=parseInt (thisHit.position), thisHitSequence;

							hitSummaryData (thisHitPosition+' - '+(thisHitPosition+thisHit.hit.length-1));
							thisHitSequence=fullSequence.substring (thisHitPosition-1, 
										       thisHitPosition+thisHit.hit.length-1);
							// cap hit sequence (and CSSD) viewing to 100 characters
							if (thisHitSequence.length>120)
							{
								thisHitSequence=thisHitSequence.substring (0, 119)+FRONTEND_MAX_STRING_DISPLAY_ELLIPSES;
							}

							return thisHitSequence;
						}
						break;
					}
				}
			}
		}
		else
		{
			hitSummaryData (FRONTEND_WHITEPSACE);
			return FRONTEND_WHITEPSACE;
		}
		hitSummaryData (FRONTEND_WHITEPSACE);
		return FRONTEND_NO_SEQUENCE_BANNER;
	}).extend({ deferred: true });
	hitCSSD=ko.computed (function () {
		if (viewingJobs ())
		{
			var thisHit=self.viewedHit ();
			if (undefined!==thisHit && undefined!==thisHit.hit)
			{
				var thisHitCSSD=thisHit.hit;

				// cap hit sequence (and CSSD) viewing to 120 characters
				if (thisHitCSSD.length>120)
				{
					thisHitCSSD=thisHitCSSD.substring (0, 119)+FRONTEND_MAX_STRING_DISPLAY_ELLIPSES;
				}

				return thisHitCSSD;
			}
		}
		else
		{
			return FRONTEND_WHITEPSACE;
		}
		return FRONTEND_NO_CSSD_BANNER;
	}).extend({ deferred: true });

	/*
	 * misc get/setters
	 */
	self.setViewingJobs=function (isViewing) {
		viewingJobs (isViewing);
		refreshViewedJobHitStats ();
	}
	self.getNumSequencesSelected=function () {
		return self.numSequencesSelected ();
	}
	self.getSequencesSelected=function () {
		return self.sequencesSelected.sort ();
	}
	self.getSequenceId=function (seqNum) {
		if (0>seqNum-1 || self.sequences ().length<seqNum)
		{
			return undefined;
		}
		else
		{
			return self.sequences ()[seqNum-1]._id;
		}
	}
	self.getSequenceAttributeFromId=function (sequenceId, attr) {
		var sequences=self.sequences ();
		for (var s=0; s<sequences.length; s++) {
			if (sequences[s]._id===sequenceId)
			{
				return sequences[s][attr];
			}
		}
		return undefined;
	}
	self.getNumCSSDsSelected=function () {
		return self.numCSSDsSelected ();
	}
	self.getCSSDsSelected=function () {
		return self.CSSDsSelected.sort ();
	}
	self.getCSSDId=function (CSSDNum) {
		if (0>CSSDNum-1 || self.CSSDs ().length<CSSDNum)
		{
			return undefined;
		}
		else
		{
			return self.CSSDs ()[CSSDNum-1]._id;
		}
	}
	self.getCSSDAttributeFromId=function (CSSDId, attr) {
		var CSSDs=self.CSSDs ();
		for (var c=0; c<CSSDs.length; c++) {
			if (CSSDs[c]._id===CSSDId)
			{
				return CSSDs[c][attr];
			}
		}
		return undefined;
	}
	self.getCSSDName=function (CSSDNum) {
                if (0>CSSDNum-1 || self.CSSDs ().length<CSSDNum)
                {
                        return undefined;
                }
                else
                {
                        return self.CSSDs ()[CSSDNum-1].name;
                }
        }
	self.getCSSDcs=function (CSSDNum) {
                if (0>CSSDNum-1 || self.CSSDs ().length<CSSDNum)
                {
                        return undefined;
                }
                else
                {
                        return self.CSSDs ()[CSSDNum-1].cs;
                }
        }
	self.isCSSDpublished=function (CSSDNum) {
		if (0>CSSDNum-1 || self.CSSDs ().length<CSSDNum)
                {
                        return undefined;
                }
                else
                {
			return self.CSSDs ()[CSSDNum-1].published;
                }
	}
	self.getNumJobsSelected=function () {
                return self.numJobsSelected ();
        }
	self.setNumJobsSelected=function (nj) {
		self.numJobsSelected (nj);
	}
        self.getJobsSelected=function () {
                return self.jobsSelected.sort ();
        }
	self.clearJobsSelected=function () {
		self.jobsSelected=[];
		self.numJobsSelected (0);
		ko.utils.arrayForEach (self.jobs(), function (j) {
			if (j.isSelected ())
			{
				j.isSelected (false);
			}
		});
	}
	self.getJobId=function (jobNum) {
                if (0>jobNum-1 || self.jobs ().length<jobNum)
                {
                        return undefined;
                }
                else
                {
                        return self.jobs ()[jobNum-1]._id;
                }
        }

	/*
	 * key handling
	 */
	self.keydownHandler=function (e) {
		if ((self.isShowingModal && !self.isShowingStatsModal) || (self.isShowingStatsModal && !(e.key==='ArrowDown' || e.key==='ArrowUp' || e.key==='Home' || e.key==='End')))
		{
			return; // don't handle key strokes if a modal is currently shown, unless its a job change while showing StatsModal;
		}

		// standard key values as per https://developer.mozilla.org/en-US/docs/Web/API/KeyboardEvent/key/Key_Values
		switch (e.key)
		{
			case 'ArrowDown' : 	var 	jobs=self.jobs (),
							currentViewedJob=self.viewedJob (),
							changed=false;
						if (undefined!==currentViewedJob)
						{
							for (this_idx=0; this_idx<jobs.length; this_idx++)
							{
								if (currentViewedJob._id===jobs[this_idx]._id)
								{
									if (this_idx<jobs.length-1)
									{
										if (e.altKey)
										{
											// scroll to the next job in job list that has at least one hit
											for (j=this_idx+1; j<jobs.length; j++)
											{
												if (jobs[j].hit_count ())
												{
													self.viewedJob (jobs[j]);
													break;
												}
											}
										}
										else
										{
											self.viewedJob (jobs[this_idx+1]);
										}
										changed=true;
									}
									break;
								}
							}
						}
						else if (jobs.length>0)
						{
							self.viewedJob (jobs[0]);
							changed=true;
						}
						if (changed)
						{
                                			var  job_id=self.viewedJob ()._id, parent_rect=undefined, child_rect=undefined;

							parent_rect=$("#job"+job_id).parent()[0].getBoundingClientRect (),
							child_rect=$("#job"+job_id)[0].getBoundingClientRect ();

							if (child_rect.y-FRONTEND_Y_TOLERANCE<parent_rect.y ||
							    (child_rect.y+child_rect.height+FRONTEND_Y_TOLERANCE)>(parent_rect.y+parent_rect.height))
							{
								$("#job"+job_id)[0].scrollIntoView ({
									behavior: "auto",
									block: "end"
								});
							}

							self.hitPage (0);
							self.hitIndex (0);
							refreshViewedJobHitStats ();
						}
						e.preventDefault ();
					   	break;
			case 'ArrowUp' : 	var 	jobs=self.jobs (),
							currentViewedJob=self.viewedJob (),
							changed=false;

						if (undefined!==currentViewedJob)
						{
							for (this_idx=0; this_idx<jobs.length; this_idx++)
							{
								if (currentViewedJob._id===jobs[this_idx]._id)
								{
									if (this_idx>0)
									{
										if (e.altKey)
										{
											// scroll to top of current job list
                                                                                        for (j=this_idx-1; j>=0; j--)
                                                                                        {
                                                                                                if (jobs[j].hit_count ())
                                                                                                {
                                                                                                        self.viewedJob (jobs[j]);
                                                                                                        break;
                                                                                                }
                                                                                        }
										}
										else
										{
											self.viewedJob (jobs[this_idx-1]);
										}
										changed=true;
									}
									break;
								}
							}
						}
						else if (jobs.length>0)
						{
							self.viewedJob (jobs[jobs.length-1]);
							changed=true;
						}
						if (changed)
						{
                                                        var  job_id=self.viewedJob ()._id,
                                                             parent_rect=$("#job"+job_id).parent()[0].getBoundingClientRect (),
                                                             child_rect=$("#job"+job_id)[0].getBoundingClientRect ();

                                                        if ((child_rect.y-FRONTEND_Y_TOLERANCE)<parent_rect.y ||
                                                            (child_rect.y+child_rect.height+FRONTEND_Y_TOLERANCE)>(parent_rect.y+parent_rect.height))
                                                        {
                                                                $("#job"+self.viewedJob ()._id)[0].scrollIntoView ({
                                                                        behavior: "auto",
                                                                        block: "start"
                                                                });
                                                        }

							self.hitPage (0);
							self.hitIndex (0);
							refreshViewedJobHitStats ();
						}
						e.preventDefault ();
					   	break;
                        case 'Home' :        	var     jobs=self.jobs ();
                                                if (jobs.length>0)
                                                {
                                                        $("#job"+jobs[0]._id)[0].scrollIntoView ({
                                                            behavior: "auto",
                                                            block: "start"
                                                        });
                                                	self.viewedJob (jobs[0]);
                                                	self.hitPage (0);
                                                	self.hitIndex (0);
							refreshViewedJobHitStats ();
						}
						e.preventDefault ();
						break;
			case 'End' :		var     jobs=self.jobs ();
                                                if (jobs.length>0)
                                                {
                                                        $("#job"+jobs[jobs.length-1]._id)[0].scrollIntoView ({
                                                            behavior: "auto",
                                                            block: "start"
                                                        });
							self.viewedJob (jobs[jobs.length-1]);
                                                	self.hitPage (0);
                                                	self.hitIndex (0);
							refreshViewedJobHitStats ();
                                                }
                                                e.preventDefault ();
                                                break;
			case 'ArrowRight' : 	var 	hits=self.hits (),
							currentViewedHit=self.viewedHit (),
							changed=false;
						if (undefined!==currentViewedHit)
						{
							for (this_idx=0; this_idx<hits.length; this_idx++)
							{
								if (currentViewedHit._id===hits[this_idx]._id)
								{
									if (this_idx<hits.length-1)
									{
										if (e.altKey)
										{
											// scroll to bottom of current hit list
											self.viewedHit (hits[hits.length-1]);
											self.hitIndex (hits.length-1);
										}			
										else
										{							
											self.viewedHit (hits[this_idx+1]);
											self.hitIndex (this_idx+1);
										}
										changed=true;
									}
									break;
								}
							}
						}
						else if (hits.length>0)
						{
							self.viewedHit (hits[0]);
							self.hitPage (0);
							self.hitIndex (0);
							refreshViewedJobHitStats ();
							changed=true;
						}
						if (changed)
						{
							var  hit_id=self.viewedHit ()._id,
                                                             parent_rect=$("#hit"+hit_id).parent()[0].getBoundingClientRect (),
                                                             child_rect=$("#hit"+hit_id)[0].getBoundingClientRect ();

                                                        if ((child_rect.y-FRONTEND_Y_TOLERANCE)<parent_rect.y ||
                                                            (child_rect.y+child_rect.height+FRONTEND_Y_TOLERANCE)>(parent_rect.y+parent_rect.height))
                                                        {
                                                                $("#hit"+hit_id)[0].scrollIntoView ({
                                                                        behavior: "auto",
                                                                        block: "end"
                                                                });
                                                        }

							refreshViewedJobHitStats ();
						}
						e.preventDefault ();
					   	break;
			case 'ArrowLeft' : 	var 	hits=self.hits (),
							currentViewedHit=self.viewedHit (),
							changed=false;
						if (undefined!==currentViewedHit)
						{
							for (this_idx=0; this_idx<hits.length; this_idx++)
							{
								if (currentViewedHit._id===hits[this_idx]._id)
								{
									if (this_idx>0)
									{
										if (e.altKey)
										{
											// scroll to top of current hit list
											self.viewedHit (hits[0]);
											self.hitIndex (0);
										}
										else
										{
											self.viewedHit (hits[this_idx-1]);
											self.hitIndex (this_idx-1);
										}
										changed=true;
									}
									break;
								}
							}
						}
						else if (hits.length>0)
						{
							self.viewedHit (hits[hits.length-1]);
							self.hitIndex (hits[hits.length-1]);
							self.hitPage (Math.ceil (hits.length/hitLimit ())-1);
							refreshViewedJobHitStats ();
							changed=true;
						}
						if (changed)
						{
							var  hit_id=self.viewedHit ()._id,
                                                             parent_rect=$("#hit"+hit_id).parent()[0].getBoundingClientRect (),
                                                             child_rect=$("#hit"+hit_id)[0].getBoundingClientRect ();

                                                        if ((child_rect.y-FRONTEND_Y_TOLERANCE)<parent_rect.y ||
                                                            (child_rect.y+child_rect.height+FRONTEND_Y_TOLERANCE)>(parent_rect.y+parent_rect.height))
                                                        {
                                                                $("#hit"+hit_id)[0].scrollIntoView ({
                                                                        behavior: "auto",
                                                                        block: "start"
                                                                });
                                                        }

						}
						e.preventDefault ();
					   	break;
			case 'PageDown' : 	var ht=hitLimit (),
					 	    nh=viewedJobNumHits (),
						    np=Math.ceil(nh/ht),		// maximum number of hit pages that are viewable
						    cp=self.hitPage ();			// current 0-indexed hit page

						if (e.altKey)
						{
							if ((cp+1)<np)
							{
								self.hitPage (np-1);
								self.hitIndex (0);
							}
							else
							{
								return;
							}
						}
						else if (((1+cp)*ht)<nh)
						{
							self.hitPage (1+cp);
							self.hitIndex (0);
						}
						else
						{
							return;
						}

						refreshViewedJobHitData (1+(self.hitPage ()*ht));						
					        refreshViewedJobHitStats ();

						e.preventDefault ();
						break;
			case 'PageUp' : 	var ht=hitLimit (),
						    nh=viewedJobNumHits (),
						    cp=self.hitPage ();

						if (0<cp)
						{
							if (e.altKey)
							{
								self.hitPage (0);
								self.hitIndex (0);
							}
							else
							{
								self.hitPage (cp-1);
								self.hitIndex (0);
							}
						}
						else
						{
							return;
						}

						refreshViewedJobHitData (1+(self.hitPage ()*ht));
						refreshViewedJobHitStats ();

						e.preventDefault ();
						break;
		}
        };
        /*
         * view handlers
         */
	self.clearSequences=function () {
		self.sequences ([]);
		self.sequencesSelected=[];
		self.numSequencesSelected (0);
	};
	self.updateSequenceEventHandlers=function () {
		/*
		 * refresh event handler for sequenceGroups
		 */
		$("[id^=navbar-seqeunces-groups-]").click (function (e) {
                	e.preventDefault ();

			var ct_id=e.currentTarget.id;
			
			if (ct_id.startsWith ("navbar-seqeunces-groups-internal-"))
			{
				ct_id=ct_id.replace ("navbar-seqeunces-groups-internal-", "");
				switch (ct_id) {
					case "All" 	 :  	var ns=0;
						     		self.sequencesSelected=[];
						     		ko.utils.arrayForEach (self.sequences(), function (s) {
									if (!s.isSelected ())
									{
										s.isSelected (true);
									}

									self.sequencesSelected.push (ns+1);

									if (!ns)
                                                			{
                                                        			$("#sequence"+s._id)[0].scrollIntoView ({
                                                            				behavior: "smooth",
                                                            				block: "start"
                                                        			});
                                                			}

									ns++;
						     		});
						     		self.numSequencesSelected (ns);
						     		break;
					case "None" 	 : 	self.sequencesSelected=[];
								var isFirst=true;
								ko.utils.arrayForEach (self.sequences(), function (s) {

									if (isFirst)
									{
                                                        			$("#sequence"+s._id)[0].scrollIntoView ({
                                                            				behavior: "smooth",
                                                            				block: "start"
                                                        			});
										isFirst=false;
									}

                                                                        if (s.isSelected ())
                                                                        {
                                                                                s.isSelected (false);
                                                                        }
                                                                });
                                                                self.numSequencesSelected (0);
								break;
					case "By-search" : 	$('#SelectSequencesBySearchModal').modal('show');
								break;
				}
			}
			else if (ct_id.startsWith ("navbar-seqeunces-groups-"))
			{
				ct_group_name=ct_id.replace ("navbar-seqeunces-groups-", '').toUpperCase ();
				var ns=0, ns_selected=0;
				self.sequencesSelected=[];
				var isFirst=true;
				ko.utils.arrayForEach (self.sequences(), function (s) {
					if (s.group.toUpperCase ()===ct_group_name)
					{
						s.isSelected (true);
                                        	self.sequencesSelected.push (ns+1);

						if (isFirst)
						{
							isFirst=false;
							$("#sequence"+s._id)[0].scrollIntoView ({
                                                            behavior: "smooth",
                                                            block: "start"
                                                        });
						}

						ns_selected++;
					}
					else
					{
						s.isSelected (false);
					}
					ns++;
				});
				self.numSequencesSelected (ns_selected);
			}
        	});
	};
	self.refreshSequences=function (sequencesJSON) {
		var seqNum=1;
		self.sequenceGroups ([]);
		self.sequences (sequencesJSON.map (function (e) {
			// truncate values
			var key, keys=Object.keys (e);
			var n=keys.length;
			var newobj={};
			while (n--) {
				key=keys[n];
				if (n!==DS_COLS_SEQUENCES._ID)
				{
					if (e[key].length>FRONTEND_MAX_STRING_DISPLAY_LENGTH)
					{
						newobj[key]=e[key].substring (0,
							FRONTEND_MAX_STRING_DISPLAY_LENGTH-FRONTEND_MAX_STRING_DISPLAY_ELLIPSES.length)+
							FRONTEND_MAX_STRING_DISPLAY_ELLIPSES;
						if (n===DS_COLS_SEQUENCES["3P_UTR_ALT"])
						{
							// in this case store full length sequence in extra FULL_NT field
							newobj["full_nt"]=e[key];
						}
					}
					else
					{
						newobj[key]=e[key];
					}
					if (n===DS_COLS_SEQUENCES.GROUP)
					{
						var thisSequenceGroups=self.sequenceGroups (), numSequenceGroups=thisSequenceGroups.length;

						var foundIdx=1;	// track arrray index that locates the group name which is equal to or greater than e[key]
						
						thisSequenceGroups.findIndex (function (el) {
							if (0>=e[key].toUpperCase ().localeCompare (el.group.toUpperCase ()))
							{
								return true;
							}
							else
							{
								foundIdx++;
								return false;
							}
						});

						if (numSequenceGroups<foundIdx || thisSequenceGroups[foundIdx-1].group!==e[key])
						{
							thisSequenceGroups.splice (foundIdx-1, 0, {group: e[key]}); // insert just before next element in sorted order
							self.sequenceGroups (thisSequenceGroups);
						}
					}
				}
				else
				{
					newobj[key]=e[key];
				}
			}
			var o=Object.assign ({}, newobj);
			o.isSelected=ko.observable (false);
			o.selectedClass=ko.computed (function () {
				return o.isSelected() ? "rna-CardContent-text-selected" : "rna-CardContent-text-not-selected";
			});
			o.seqNum=seqNum++;
			return o;
		}));
		self.sequencesSelected=[];
		self.numSequencesSelected (0);
	};
	self.selectSequencesBySearchTerm=async function () {
		var searchTerm=$("#select-sequences-by-search-table-term").val ();
		if (undefined===searchTerm || !searchTerm.trim ().length)
		{
			return;
		}

		searchTerm=searchTerm.trim ().toUpperCase ();

		var ns=0, ns_selected=0;
		self.sequencesSelected=[];
		var isFirst=true;
		ko.utils.arrayForEach (self.sequences(), function (s) {

			var this_seq_nt=s.full_nt;
			if (undefined===this_seq_nt)
			{
				this_seq_nt=s.seqnt;
			}

			if (s.accession.toUpperCase ().includes (searchTerm) ||
			    s.definition.toUpperCase ().includes (searchTerm) ||
			    this_seq_nt.toUpperCase ().includes (searchTerm) ||
			    s.group.toUpperCase ().includes (searchTerm))
			{
				s.isSelected (true);
				self.sequencesSelected.push (ns+1);

				if (isFirst)
				{
					isFirst=false;
					$("#sequence"+s._id)[0].scrollIntoView ({
					    behavior: "smooth",
					    block: "start"
					});
				}

				ns_selected++;
			}
			else
			{
				s.isSelected (false);
			}
			ns++;
		});
		self.numSequencesSelected (ns_selected);

		$("#SelectSequencesBySearchModal").modal ("hide");

		await sleep (10); // allow modal to hide

		if (!ns_selected)
		{
			alert ("no matching sequences found");
		}
	};
	self.selectCSSDsBySearchTerm=async function () {
		var searchTerm=$("#select-CSSDs-by-search-table-term").val ();
		if (undefined===searchTerm || !searchTerm.trim ().length)
		{
			return;
		}

		searchTerm=searchTerm.trim ().toUpperCase ();

		var nc=0, nc_selected=0;
		self.CSSDsSelected=[];
		var isFirst=true;
		ko.utils.arrayForEach (self.CSSDs(), function (c) {

			var cs=c.cs, name=c.name;

			if (FRONTEND_CSSD_STRING_SEPARATOR_HTML_PREFIX && FRONTEND_CSSD_STRING_SEPARATOR_HTML_PREFIX.length>0)
			{
				cs=cs.replace (FRONTEND_CSSD_STRING_SEPARATOR_HTML_PREFIX, '');
			}
			if (FRONTEND_CSSD_STRING_SEPARATOR_HTML_SUFFIX && FRONTEND_CSSD_STRING_SEPARATOR_HTML_SUFFIX.length>0)
			{
				cs=cs.replace (FRONTEND_CSSD_STRING_SEPARATOR_HTML_SUFFIX, '');
			}
			cs=cs.replace (FRONTEND_WHITEPSACE, ' ');

			if (FRONTEND_CSSD_NAME_SEPARATOR_HTML_PREFIX && FRONTEND_CSSD_NAME_SEPARATOR_HTML_PREFIX.length>0)
			{
				name=name.replace (FRONTEND_CSSD_NAME_SEPARATOR_HTML_PREFIX, '');
			}
			if (FRONTEND_CSSD_NAME_SEPARATOR_HTML_SUFFIX && FRONTEND_CSSD_NAME_SEPARATOR_HTML_SUFFIX.length>0)
			{
				name=name.replace (FRONTEND_CSSD_NAME_SEPARATOR_HTML_SUFFIX, '');
			}

			if (cs.toUpperCase().includes (searchTerm) || name.toUpperCase().includes (searchTerm))
			{
				c.isSelected (true);
				self.CSSDsSelected.push (nc+1);

				if (isFirst)
				{
					isFirst=false;
					$("#CSSD"+c._id)[0].scrollIntoView ({
					    behavior: "smooth",
					    block: "start"
					});
				}

				nc_selected++;
			}
			else
			{
				c.isSelected (false);
			}
			nc++;
		});
		self.numCSSDsSelected (nc_selected);

		$("#SelectCSSDsBySearchModal").modal ("hide");

		await sleep (10); // allow modal to hide

		if (!nc_selected)
		{
			alert ("no matching CSSDs found");
		}
	};
	self.selectJobsBySearchTerm=async function () {
		var searchTerm=$("#select-jobs-by-search-table-term").val ();
		if (undefined===searchTerm || !searchTerm.trim ().length)
		{
			return;
		}

		searchTerm=searchTerm.trim ().toUpperCase ();

		var nj=0, nj_selected=0;
		self.jobsSelected=[];
		var isFirst=true;
		ko.utils.arrayForEach (self.jobs(), function (j) {

			if (j.formatted_id.includes (searchTerm) || 
			    j.name.toUpperCase ().includes (searchTerm) ||
			    j.definition.toUpperCase ().includes (searchTerm))
			{
				j.isSelected (true);
				self.jobsSelected.push (nj+1);

				if (isFirst)
				{
					isFirst=false;
					$("#job"+j._id)[0].scrollIntoView ({
					    behavior: "smooth",
					    block: "start"
					});

                                        self.viewedJob (j);
                                        self.hitPage (0);
                                        self.hitIndex (0);
					refreshViewedJobHitStats ();
				}

				nj_selected++;
			}
			else
			{
				j.isSelected (false);
			}
			nj++;
		});
		self.numJobsSelected (nj_selected);

		$("#SelectJobsBySearchModal").modal ("hide");

		await sleep (10); // allow modal to hide

		if (!nj_selected)
		{
			alert ("no matching jobs found");
		}
	};
	self.clearCSSDs=function () {
		self.CSSDs ([]);
		self.CSSDsSelected=[];
		self.numCSSDsSelected (0);
	};
        self.updateCSSDEventHandlers=function () {
                /*
                 * refresh event handler for CSSD search
                 */
                $("[id^=navbar-CSSDs-]").click (function (e) {
                        e.preventDefault ();

                        var CSSD_id=e.currentTarget.id;

                        if (CSSD_id.startsWith ("navbar-CSSDs-internal-"))
                        {
                                CSSD_id=CSSD_id.replace ("navbar-CSSDs-internal-", "");
                                switch (CSSD_id) {
                                        case "All"       :      var nc=0;
                                                                self.CSSDsSelected=[];
                                                                ko.utils.arrayForEach (self.CSSDs(), function (c) {
                                                                        if (!c.isSelected ())
                                                                        {
                                                                                c.isSelected (true);
                                                                        }

                                                                        self.CSSDsSelected.push (nc+1);

                                                                        if (!nc)
                                                                        {
                                                                                $("#CSSD"+c._id)[0].scrollIntoView ({
                                                                                        behavior: "smooth",
                                                                                        block: "start"
                                                                                });
                                                                        }

                                                                        nc++;
                                                                });
                                                                self.numCSSDsSelected (nc);
                                                                break;
                                        case "None"      :      self.CSSDsSelected=[];
                                                                var isFirst=true;
                                                                ko.utils.arrayForEach (self.CSSDs(), function (c) {

                                                                        if (isFirst)
                                                                        {
                                                                                $("#CSSD"+c._id)[0].scrollIntoView ({
                                                                                        behavior: "smooth",
                                                                                        block: "start"
                                                                                });
                                                                                isFirst=false;
                                                                        }

                                                                        if (c.isSelected ())
                                                                        {
                                                                                c.isSelected (false);
                                                                        }
                                                                });
                                                                self.numCSSDsSelected (0);
                                                                break;
                                        case "By-search" :      $('#SelectCSSDsBySearchModal').modal('show');
                                                                break;
                                }
                        }
                });
        }
	self.refreshCSSDs=function (cssdsJSON) {
		var CSSDNum=1;
		self.CSSDs (cssdsJSON.map (function (e) {
			// truncate values
			var key, keys=Object.keys (e);
			var n=keys.length;
			var newobj={};
			while (n--) {
				key=keys[n];
				if (n!==DS_COLS_CSSD._ID)
				{
					if (n===DS_COLS_CSSD.REF_ID)
                                        {
                                              	newobj[key]=e[key];
                                        }
					else if (n===DS_COLS_CSSD.PUBLISHED)
					{
						if (self.hasCapability ('CSSD', 'EDIT')())
						{
							newobj[key]=e[key]==='1'; 
						}
						else
						{
							newobj[key]=false; // only provide actual "publish" status if capability granted (i.e. a curator)
						}
					}
					else if (n===DS_COLS_CSSD.STRING)
					{
						model_parts=e[key].replace(/ /g, FRONTEND_WHITEPSACE).split (FRONTEND_NEWLINE_CHAR);
						newobj[key]='';
						for (var i=0; i<model_parts.length; i++)
						{
							newobj[key]=newobj[key].concat (FRONTEND_CSSD_STRING_SEPARATOR_HTML_PREFIX,
											model_parts[i],
											FRONTEND_CSSD_STRING_SEPARATOR_HTML_SUFFIX);
						}
					}
					else if (n===DS_COLS_CSSD.NAME)
					{
						if (e[key].length>FRONTEND_MAX_STRING_DISPLAY_LENGTH)
						{
							// break up line into max 2. if necessary, second line to have ellipses
							var posn=Math.min (FRONTEND_MAX_STRING_DISPLAY_LENGTH, Math.floor (e[key].length/2));
							while (e[key][posn]!=' ' && posn<e[key].length)
							{
								posn++;
							}
							var firstLine=e[key].substring (0, posn);							
							var secondLine;
							if (e[key].length-posn>FRONTEND_MAX_STRING_DISPLAY_LENGTH)
							{
								secondLine=e[key].substring (posn+1, 
									   posn+FRONTEND_MAX_STRING_DISPLAY_LENGTH-FRONTEND_MAX_STRING_DISPLAY_ELLIPSES.length)+
									   FRONTEND_MAX_STRING_DISPLAY_ELLIPSES;
							}
							else
							{
								secondLine=e[key].substring (posn+1, e[key].length);
							}
							newobj[key]=FRONTEND_CSSD_NAME_SEPARATOR_HTML_PREFIX+
								    firstLine+
								    FRONTEND_CSSD_NAME_SEPARATOR_HTML_SUFFIX+
								    FRONTEND_CSSD_NAME_SEPARATOR_HTML_PREFIX+
								    secondLine+
								    FRONTEND_CSSD_NAME_SEPARATOR_HTML_SUFFIX;
						}
						else
						{
							newobj[key]=FRONTEND_CSSD_NAME_SEPARATOR_HTML_PREFIX+
								    e[key]+
								    FRONTEND_CSSD_NAME_SEPARATOR_HTML_SUFFIX;
						}
					}
				}
				else
				{
					newobj[key]=e[key];
				}
			}			

			var o=Object.assign ({}, newobj);
			o.isSelected=ko.observable (false);
			o.selectedClass=ko.computed (function () {
				return o.isSelected() ? "rna-CardContent-text-selected" : "rna-CardContent-text-not-selected";
			});
			o.CSSDNum=CSSDNum++;

			return o;
		}));
		self.CSSDsSelected=[];
		self.numCSSDsSelected (0);
	};
        self.updateJobEventHandlers=function () {
                /*
                 * refresh event handler for job search
                 */
                $("[id^=navbar-jobs-]").click (function (e) {
                        e.preventDefault ();

                        var job_id=e.currentTarget.id;

                        if (job_id.startsWith ("navbar-jobs-internal-"))
                        {
                                job_id=job_id.replace ("navbar-jobs-internal-", "");
                                switch (job_id) {
                                        case "All"       :      var nj=0;
                                                                self.jobsSelected=[];
                                                                ko.utils.arrayForEach (self.jobs(), function (j) {
                                                                        if (!j.isSelected ())
                                                                        {
                                                                                j.isSelected (true);
                                                                        }

                                                                        self.jobsSelected.push (nj+1);

                                                                        if (!nj)
                                                                        {
                                                                                $("#job"+j._id)[0].scrollIntoView ({
                                                                                        behavior: "smooth",
                                                                                        block: "start"
                                                                                });
                                                                        }

                                                                        nj++;
                                                                });
                                                                self.numJobsSelected (nj);
                                                                break;
                                        case "None"      :      self.jobsSelected=[];
                                                                var isFirst=true;
                                                                ko.utils.arrayForEach (self.jobs(), function (j) {

                                                                        if (isFirst)
                                                                        {
                                                                                $("#job"+j._id)[0].scrollIntoView ({
                                                                                        behavior: "smooth",
                                                                                        block: "start"
                                                                                });
                                                                                isFirst=false;
                                                                        }

                                                                        if (j.isSelected ())
                                                                        {
                                                                                j.isSelected (false);
                                                                        }
                                                                });
                                                                self.numJobsSelected (0);
                                                                break;
                                        case "By-search" :      $('#SelectJobsBySearchModal').modal('show');
                                                                break;
                                }
                        }
                });
        }
	self.refreshJobs=async function (jobsJSON) {					// refresh ALL jobs
		var haveViewedJob=false,
		    previousJobList=self.jobs (),
		    previousJobListSize=previousJobList.length,
		    jobNum=1;

		self.jobs (jobsJSON.map (function (e) {

			var hit_count=ko.observable (0), hit_time=ko.observable (0.0);

			if (previousJobListSize)
			{
				// re-initialize hit_count with previous job list's hit_count - avoid multiple feather icon updates
				for (var j=0; j<previousJobListSize; j++)
				{
					if (previousJobList[j]._id===e._id)
					{
						hit_count (previousJobList[j].hit_count ());
						break;
					}
				}
			}

			var o={
				'_id' 		: e._id,
				'formatted_id' 	: e._id.substring (0,2)+'-'+e._id.substring (2,4)+'-'+e._id.substring (4,6)+' '+
						  e._id.substring (6,8)+':'+e._id.substring (8,10)+':'+e._id.substring (10,12),
				'name'		: e.name,
				'definition' 	: e.definition,
				'status' 	: ko.observable (''.concat (e.num_windows>0?''.concat (Math.round (e.num_windows_success*100.0/e.num_windows).toString(),'%'):'0%', ' complete')),
				'isDone' 	: ko.observable (e.status===DS_JOB_STATUS_DONE),
				'isStarted'	: ko.observable (e.status===DS_JOB_STATUS_PENDING || e.status===DS_JOB_STATUS_SUBMITTED),
				'isSuccess'	: ko.observable (e.error===DS_JOB_ERROR_OK),
				'sequence_id' 	: e.sequence_id,
				'cssd_id' 	: e.cssd_id,
				'hit_count'	: hit_count,
				'hit_time'	: hit_time,
				'start_time'	: moment (),
				'compute_status': ko.pureComputed (function () {
							if (o.isDone ())
							{
								return 'CPU time is '+o.hit_time ()+' s';
							}
							else if (o.isStarted())
							{
								return o.hit_count ()+' hits, CPU time is '+o.hit_time ()+' s';
							}
							else
							{
								return '';
							}
						  })
			};

			refreshJobHitCount (e._id, hit_count);
			refreshJobHitTime (e._id, hit_time);

			o.isSelected=ko.observable (false);
			o.selectedClass=ko.computed (function () {
				return o.isSelected() ? "rna-CardContent-text-selected" : "rna-CardContent-text-not-selected";
			});
			o.viewedClass=ko.computed (function () {
				var thisJob=self.viewedJob ();
				if (undefined!==thisJob && o._id===thisJob._id)
				{
					haveViewedJob=true;
					return "rna-CardContent-border-selected";
				}
				else
				{
					return "rna-CardContent-border-not-selected";
				}
			});
                        o.jobNum=jobNum++;

			return o;
		}));

		var currentJobList=self.jobs (), currentJobListSize=currentJobList.length;

		if (!haveViewedJob || currentJobListSize!==previousJobListSize)
		{
			if (currentJobListSize)
			{
				self.viewedJob (currentJobList[currentJobListSize-1]);
				var hits=self.hits ();
				if (hits.length>0)
				{
					self.viewedHit (hits[0]);
				}
				else
				{
					self.viewedHit (undefined);
				}
			}
			else
			{
				self.viewedJob (undefined);
				self.viewedHit (undefined);
			}
			self.hitIndex (0);
                        self.hitPage (0);
		}
		self.jobsSelected=[];
                self.numJobsSelected (0);
		refreshViewedJobHitStats ();

		if (1>currentJobListSize)
		{
			return;
		}

		if (currentJobListSize!==previousJobListSize)
		{
			while (undefined===$("#job"+currentJobList[currentJobListSize-1]._id)[0])
                        {
                                await sleep (10);
                        }

			$("#job"+currentJobList[currentJobListSize-1]._id)[0].scrollIntoView ({
				behavior: "auto",
				block: "end"
			});
		}
	};
	self.refreshJob=async function (jobsJSON) {			// refresh SINGLE job		
		var haveViewedJob=false,
		    previousJobList=self.jobs (),
		    previousJobListSize=previousJobList.length;

		/*
		 * job entry already exists?
		 */
		if (previousJobListSize)
		{
			for (var j=0; j<previousJobListSize; j++)
			{
				if (previousJobList[j]._id===jobsJSON[0]._id)
				{
                                        // yes, create new object and update any fields ifrom previousJobList that can get updates
                                        previousJobList[j].status (''.concat (jobsJSON[0].num_windows>0?''.concat (Math.round (jobsJSON[0].num_windows_success*100.0/jobsJSON[0].num_windows).toString(),'%'):'0%', ' complete'));
                                        previousJobList[j].isDone (jobsJSON[0].status===DS_JOB_STATUS_DONE);
                                        previousJobList[j].isStarted (jobsJSON[0].status===DS_JOB_STATUS_PENDING || jobsJSON[0].status===DS_JOB_STATUS_SUBMITTED);
                                        previousJobList[j].isSuccess (jobsJSON[0].error===DS_JOB_ERROR_OK);
                                        refreshJobHitCount (previousJobList[j]._id, previousJobList[j].hit_count);
					refreshJobHitTime (previousJobList[j]._id, previousJobList[j].hit_time);
                                        self.jobs (previousJobList);

					return;
				}
			}
		}

		/*
		 * no, so add a new entry
		 */
		var sequences=self.sequences(), sequencesLength=sequences.length,
		    CSSDs=self.CSSDs(), CSSDsLength=CSSDs.length,
		    name=undefined,
		    definition=undefined;

		for (var s=0; s<sequencesLength; s++)
		{
			if (sequences[s]._id===jobsJSON[0].sequence_id)
			{
				definition=sequences[s].definition;
				break;
			}
		}
		for (var c=0; c<CSSDsLength; c++)
		{
			if (CSSDs[c]._id===jobsJSON[0].cssd_id)
			{
				name=CSSDs[c].name;
				break;
			}
		}

		if (undefined===name || undefined===definition)
		{
			return;						// deleted sequence/CSSD/spurious job?
		}

		var hit_count=ko.observable (0),
		    hit_time=ko.observable (0.0),
		    o={
			'_id' 		: jobsJSON[0]._id,
			'formatted_id' 	: jobsJSON[0]._id.substring (0,2)+'-'+jobsJSON[0]._id.substring (2,4)+'-'+jobsJSON[0]._id.substring (4,6)+' '+
					  jobsJSON[0]._id.substring (6,8)+':'+jobsJSON[0]._id.substring (8,10)+':'+jobsJSON[0]._id.substring (10,12),
			'name'		: name,
			'definition' 	: definition,
			'status' 	: ko.observable (''.concat (jobsJSON[0].num_windows>0?''.concat (Math.round (jobsJSON[0].num_windows_success*100.0/jobsJSON[0].num_windows).toString(),'%'):'0%', ' complete')),
			'isDone' 	: ko.observable (jobsJSON[0].status===DS_JOB_STATUS_DONE),
			'isStarted'	: ko.observable (jobsJSON[0].status===DS_JOB_STATUS_PENDING || jobsJSON[0].status===DS_JOB_STATUS_SUBMITTED),
			'isSuccess'	: ko.observable (jobsJSON[0].error===DS_JOB_ERROR_OK),
			'sequence_id' 	: jobsJSON[0].sequence_id,
			'cssd_id' 	: jobsJSON[0].cssd_id,
			'hit_count'	: hit_count,
			'hit_time' 	: hit_time,
			'start_time' 	: moment (),
			'compute_status': ko.pureComputed (function () {
						if (o.isDone ())
						{
							return 'CPU time is '+o.hit_time ()+' s';
						}
						else if (o.isStarted())
						{
							return o.hit_count ()+' hits, CPU time is '+o.hit_time ()+' s';
						}
						else
						{
							return '';
						}
					  })
		};

		refreshJobHitCount (jobsJSON[0]._id, hit_count);
		refreshJobHitTime (jobsJSON[0]._id, hit_time);

		o.isSelected=ko.observable (false);
		o.selectedClass=ko.computed (function () {
			return o.isSelected() ? "rna-CardContent-text-selected" : "rna-CardContent-text-not-selected";
		});
		o.viewedClass=ko.computed (function () {
			var thisJob=self.viewedJob ();
			if (undefined!==thisJob && o._id===thisJob._id)
			{
				haveViewedJob=true;
				return "rna-CardContent-border-selected";
			}
			else
			{
				return "rna-CardContent-border-not-selected";
			}
		});
                o.jobNum=previousJobListSize+1;

		self.jobs.push (o);
		self.viewedJob (o);
		self.jobsSelected=[];
                self.numJobsSelected (0);
		ko.utils.arrayForEach (self.jobs(), function (j) {
			if (j.isSelected ())
			{
				j.isSelected (false);
			}
		});

		var hits=self.hits ();
		if (hits.length>0)
		{
			self.viewedHit (hits[0]);
		}
		else
		{
			self.viewedHit (undefined);
		}
		self.hitIndex (0);                      
		self.hitPage (0);                       
		refreshViewedJobHitStats ();

		if (self.scrollIntoViewEnabled)
		{
			while (undefined===$("#job"+o._id)[0])
			{
				await sleep (10);			// quick 'hack' to detect knockout has bound the newe DOM element;
									// todo: use ko binding lifecycle events
			}

			$("#job"+o._id)[0].scrollIntoView ({
				behavior: "auto",
				block: "end"
			});
		}
	};
	self.updateHitEventHandlers=function () {
		$("[id^=navbar-hits-show-dropdown-item-]").click (function (e) {
			e.preventDefault();

			var hl_strn=e.currentTarget.id.replace ("navbar-hits-show-dropdown-item-", ""), hl=FRONTEND_DEFAULT_DS_LIMIT; // deduce number of hits to limit from id, but default to 10

			try {
				hl=parseInt (hl_strn);
			} catch (err) { };

			if (hl!==hitLimit ())
			{
				hitLimit (hl);

				RNAkoModel.hitPage (0);
				RNAkoModel.hitIndex (0);
				refreshViewedJobHitData ();
				refreshViewedJobHitStats ();
			}
		});
	}
	self.refreshHits=async function (hitsJSON) {
		var haveViewedHit=false;
		self.hits (hitsJSON.map (function (e) {
			var o={
				'_id' 		: e._id,
				'position'	: e.position,
				'fe'		: parseFloat (e.fe).toFixed (2),
				'hit'		: e.hit_string
			};
			o.isSelected=ko.observable (false);
			o.selectedClass=ko.computed (function () {
				return o.isSelected() ? "rna-CardContent-text-selected" : "rna-CardContent-text-not-selected";
			});
			o.viewedClass=ko.computed (function () {
				var thisHit=self.viewedHit ();
				if (undefined!==thisHit && o._id===thisHit._id)
				{
					haveViewedHit=true;
					return "rna-CardContent-border-selected";
				}
				else
				{
					return "rna-CardContent-border-not-selected";
				}
			});
			return o;
		}));
		if (!haveViewedHit)
		{
			if (self.hits ().length)
			{
				self.viewedHit (self.hits ()[0]);
				while (undefined===$("#hit"+self.viewedHit ()._id)[0])
				{
					await sleep (10);                       
				}

				$("#hit"+self.viewedHit ()._id)[0].scrollIntoView ({
				    behavior: "auto",
				    block: "start"
				});
			}
			else
			{
				self.viewedHit (undefined);
			}
		}
	};	
	self.refreshViewedJobHitStats=function (jobId, hitStatsJSON) {
		if (undefined===jobId || jobId!==self.getViewedJobId () || !isCardOn ("#viewJobsCard"))
		{
			$("#statsButton").hide ();
			return;
		}

		var l=self.viewedJobFullSequenceLength();
		if (undefined===l || 1>l)
		{
                        $("#statsButton").hide ();
			return;
		}

		var hitStats="";
		hitStatsJSON.map (function (h) {
			var p_int=parseInt (h['position']), 
			    p_x=24.82+(p_int*((599.27-24.82)/l)), 
			    p_y1=239.26+(Math.min (Math.max(h['min_fe'], -50), 5) * ((239.26-8.26)/50)), 
			    p_y2=239.26+(Math.min (Math.max(h['max_fe'], -50), 5) * ((239.26-8.26)/50)), 
			    p_y3=239.26+(Math.min (Math.max(h['avg_fe'], -50), 5) * ((239.26-8.26)/50)),
			    hit_count=h['count'];

			// vertical line spanning min-mean-max values
			hitStats=hitStats+"<path d='M"+p_x+" "+p_y1+"L"+p_x+" "+p_y2+"'opacity='0.1' fill-opacity='0' stroke='#660000' stroke-width='1' stroke-opacity='1'></path>";

			// add path around min value (and a larger, transparent circle for mouseover/out event detection)
			hitStats=hitStats+"<path d='M"+(p_x-1)+" "+p_y1+"L"+(p_x+1)+" "+p_y1+"' stroke-width='1' opacity='0.1' fill-opacity='0' stroke='#660000' stroke-opacity='1'></path>"+
					  "<circle id='statCircleMin"+p_int+"' cx='"+p_x+"' cy='"+p_y1+"' r=4 stroke-width=0 fill='gray' opacity='0.0'>"+
						p_int+','+
						h['min_fe']+','+
					 	h['avg_fe']+','+
						h['max_fe']+','+
						h['std_fe']+','+
						hit_count+"</circle>";

			// add path around max value
                        hitStats=hitStats+"<path d='M"+(p_x-1)+" "+p_y2+"L"+(p_x+1)+" "+p_y2+"' stroke-width='1' opacity='0.1' fill-opacity='0' stroke='#660000' stroke-opacity='1'></path>"+
					  "<circle id='statCircleMax"+p_int+"' cx='"+p_x+"' cy='"+p_y2+"' r=4 stroke-width=0 fill='gray' opacity='0.0'>"+
						p_int+','+
						h['min_fe']+','+
                                                h['avg_fe']+','+
                                                h['max_fe']+','+
						h['std_fe']+','+
                                                hit_count+"</circle>";

			// add circle around mean value
			hitStats=hitStats+"<circle cx='"+p_x+"' cy='"+p_y3+"' r=1 stroke-width=0 fill='red' opacity='0.7'></circle>"+
					  "<circle id='statCircleAve"+p_int+"' cx='"+p_x+"' cy='"+p_y3+"' r=4 stroke-width=0 fill='red' opacity='0.0'>"+
						p_int+','+
						h['min_fe']+','+
                                                h['avg_fe']+','+
                                                h['max_fe']+','+
						h['std_fe']+','+
                                                hit_count+"</circle>";
		});

		// add sequence length
		hitStats+='<text x="603.89" y="283.77" font-size="6" font-family="Open Sans" font-weight="normal" font-style="normal" letter-spacing="0" alignment-baseline="before-edge" transform="matrix(1 0 0 1 -4.915904198062435 -42.31431646932185)" style="line-height:100%" xml:space="preserve" dominant-baseline="text-before-edge" opacity="1" fill="#b0c4de" fill-opacity="1" text-anchor="end">'+self.viewedJobFullSequenceLength()+'</text>';

		if (hitStats.length>0)
		{
			var buttonStatsElement=document.getElementById ('statsButtonSVG');
			if (buttonStatsElement.children.length>1)
			{
				buttonStatsElement.children[buttonStatsElement.children.length-1].innerHTML=hitStats;
                                buttonStatsElement.children[buttonStatsElement.children.length-2].innerHTML='<g></g>';
			}
		
			var modalStatsElement=document.getElementById ('statsModalSVG');
			if (modalStatsElement.children.length>1)
			{
				modalStatsElement.children[modalStatsElement.children.length-1].innerHTML=hitStats;
				modalStatsElement.children[modalStatsElement.children.length-2].innerHTML='<g></g>';
			}

			$("#statsButton").show ();

			$("[id^=statCircleMax").mousedown (function (event) {
				var thisPosnTextContent=event.target.textContent.split (','),
                                    thisPosn=thisPosnTextContent[0],
				    thisFE=Number.parseFloat(thisPosnTextContent[3]);

				getViewedJobHitIndex (thisPosn, thisFE);
			});
                        $("[id^=statCircleMin").mousedown (function (event) {
                                var thisPosnTextContent=event.target.textContent.split (','),
                                    thisPosn=thisPosnTextContent[0],
                                    thisFE=Number.parseFloat(thisPosnTextContent[1]);

                                getViewedJobHitIndex (thisPosn, thisFE);
                        });
                        $("[id^=statCircleAve").mousedown (function (event) {
                                var thisPosnTextContent=event.target.textContent.split (','),
                                    thisPosn=thisPosnTextContent[0],
                                    thisFE=Number.parseFloat(thisPosnTextContent[2]);

                                getViewedJobHitIndex (thisPosn, thisFE);
                        });

			$("[id^=statCircle").mouseover (function (event) {
				var thisPosnTextContent=event.target.textContent.split (','),
				    thisPosn=thisPosnTextContent[0],
				    thisPosnX=event.target.cx.baseVal.value,
				    thisPosnY=event.target.cy.baseVal.value,
				    thisPosnYMax=$("[id^=statCircleMax"+thisPosn)[0].cy.baseVal.value,
				    thisPosnStatsContent="count "+thisPosnTextContent[5]+"   "+
							 (event.target.id.indexOf ("Min")>=0?
							 "  | min "+Number.parseFloat(thisPosnTextContent[1]).toFixed(2)+" | ":
							 "    min "+Number.parseFloat(thisPosnTextContent[1]).toFixed(2)+"   ")+
							 (event.target.id.indexOf ("Ave")>=0?
							 "  | avg "+Number.parseFloat(thisPosnTextContent[2]).toFixed(2)+" | ":
							 "    avg "+Number.parseFloat(thisPosnTextContent[2]).toFixed(2)+"   ")+
							 (event.target.id.indexOf ("Max")>=0?
							 "  | max "+Number.parseFloat(thisPosnTextContent[3]).toFixed(2)+" | ":
						         "    max "+Number.parseFloat(thisPosnTextContent[3]).toFixed(2)+"   ")+
							 "    std "+Number.parseFloat(thisPosnTextContent[4]).toFixed(2),
				    thisPosnData;

				thisPosnData=
					// add sequence posn below x-axis
					'<text x="'+thisPosnX+'" y="301" font-size="5" font-family="Open Sans" font-weight="bold" font-style="normal" letter-spacing="0" alignment-baseline="before-edge" transform="matrix(1 0 0 1 3 -42.31431646932185)" style="line-height:100%" xml:space="preserve" dominant-baseline="text-before-edge" opacity="0.4" fill="red" fill-opacity="1" text-anchor="middle">'+thisPosn+'</text>'+
					// add sequence posn marker along x-axis
					'<path d="M'+thisPosnX+' 242.26L'+thisPosnX+' 236.26" opacity="1" fill-opacity="0" stroke="#b6b6b6" stroke-width="1" stroke-opacity="0.2"></path>'+
					// add fe marker along y-axis
					'<path d="M26.82 '+thisPosnY+'L22.82 '+thisPosnY+'" opacity="1" fill-opacity="0" stroke="#b6b6b6" stroke-width="1" stroke-opacity="0.2"></path>'+
					// add posn guide for x-axis (vertically)
					((thisPosnYMax<=244||thisPosnYMax>=250)?
					('<line stroke="#b6b6b6" stroke-width="1" stroke-opacity="0.1" stroke-dasharray="5, 4" x1="'+thisPosnX+'" y1="234" x2="'+thisPosnX+'" y2="'+(thisPosnYMax+4)+'"></line>'):(''))+
					// add posn guide for y-axis (horizontally)
                                        ((thisPosnX>=47)?
                                        ('<line stroke="#b6b6b6" stroke-width="1" stroke-opacity="0.1" stroke-dasharray="5, 4" x1="28.82" y1="'+thisPosnY+'" x2="'+(thisPosnX-7)+'" y2="'+thisPosnY+'"></line>'):(''))+
					// add a circular marker around target
					'<circle cx="'+thisPosnX+'" cy="'+thisPosnY+'" r=3 stroke-width=1 stroke="#660000" fill="red" stroke-opacity="0.2" fill-opacity="0"></circle>';
				   
				if (thisPosnX>311)	// position along x-axis is more/less than half-way?
				{
					thisPosnData+=
						'<text x="'+(thisPosnX-10)+'" y="301" font-size="5" font-family="Open Sans" font-weight="normal" font-style="normal" letter-spacing="0" alignment-baseline="before-edge" transform="matrix(1 0 0 1 -4.915904198062435 -42.31431646932185)" style="line-height:100%" xml:space="preserve" dominant-baseline="text-before-edge" opacity="0.8" fill="gray" fill-opacity="1" text-anchor="end">'+thisPosnStatsContent+'</text>';
				}
				else
				{
					thisPosnData+=
                                                '<text x="'+(thisPosnX+28)+'" y="301" font-size="5" font-family="Open Sans" font-weight="normal" font-style="normal" letter-spacing="0" alignment-baseline="before-edge" transform="matrix(1 0 0 1 -4.915904198062435 -42.31431646932185)" style="line-height:100%" xml:space="preserve" dominant-baseline="text-before-edge" opacity="0.8" fill="gray" fill-opacity="1" text-anchor="start">'+thisPosnStatsContent+'</text>';
				}

				if (buttonStatsElement.children.length>1)
                        	{
                                	buttonStatsElement.children[buttonStatsElement.children.length-2].innerHTML=thisPosnData;
                        	}

				if (modalStatsElement.children.length>1)
                        	{
                                	modalStatsElement.children[modalStatsElement.children.length-2].innerHTML=thisPosnData;
                        	}
			});
			$("[id^=statCircle]").mouseout (function () {
				// remove all data and replace (last) place-holder
				if (buttonStatsElement.children.length>1)
                                {
                                        buttonStatsElement.children[buttonStatsElement.children.length-2].innerHTML='<g></g>';
                                }

                                if (modalStatsElement.children.length>1)
                                {
                                        modalStatsElement.children[modalStatsElement.children.length-2].innerHTML='<g></g>';
                                }
			});
		}
		else
		{
			$("#statsButton").hide ();
		}
	}

	self.toggleSequences=function (row) {
		row.isSelected (!row.isSelected ());
		if (row.isSelected ())
		{
			self.numSequencesSelected (self.numSequencesSelected () + 1);
			// add this seqNum to sequencesSelected
			if (!(self.sequencesSelected.includes (row.seqNum)))
			{
				self.sequencesSelected.push (row.seqNum);
			}
		}
		else
		{
			self.numSequencesSelected (self.numSequencesSelected () - 1);
			// splice out seqNum from sequencesSelected
			for (var i=0; i<self.sequencesSelected.length; i++)
			{ 
			   if (self.sequencesSelected[i]===row.seqNum)
			   {
				self.sequencesSelected.splice (i, 1); 
			   }
			}
		}
	}
	self.findSequenceByName=async function (row) {
                var sequences=self.sequences ();
                for (var i=0; i<sequences.length; i++)
                {
                        if (sequences[i].definition===row.definition)
                        {
				// wait for scrolling to finish and client be visible
				var  parent_rect=$("#sequence"+sequences[i]._id).parent()[0].getBoundingClientRect (),
                                     child_rect=$("#sequence"+sequences[i]._id)[0].getBoundingClientRect ();

				if ((child_rect.y-FRONTEND_Y_TOLERANCE)<parent_rect.y ||
                                    (child_rect.y+child_rect.height+FRONTEND_Y_TOLERANCE)>(parent_rect.y+parent_rect.height))
                                {
                                	$("#sequence"+ sequences[i]._id)[0].scrollIntoView ({
                                        	behavior: "smooth",
                                        	block: "start"
                                	});

					var num_attempts=100;
					while (--num_attempts)
					{
                                                await sleep (FRONTEND_HIGHLIGHT_FLASH_DELAY_MS);        // always wait a bit	

						child_rect=$("#sequence"+sequences[i]._id)[0].getBoundingClientRect ();

						if ((child_rect.y+FRONTEND_Y_TOLERANCE)>=parent_rect.y &&
					    	    (child_rect.y+child_rect.height-FRONTEND_Y_TOLERANCE)<=(parent_rect.y+parent_rect.height))
						{
							// child is now within visible rect (of parent)
							break;
						}
					}
				}

				await sleep (FRONTEND_HIGHLIGHT_FLASH_DELAY_MS);

                                for (var r=0; r<FRONTEND_HIGHLIGHT_FLASH_REPEAT; r++)
                                {
                                        sequences[i].isSelected (!sequences[i].isSelected ());
                                        await sleep (FRONTEND_HIGHLIGHT_FLASH_DELAY_MS);		
                                        sequences[i].isSelected (!sequences[i].isSelected ());
				        await sleep (FRONTEND_HIGHLIGHT_FLASH_DELAY_MS);	
                                }
                                break;
                        }
                }
        }
	self.toggleCSSD=function (row) {
		row.isSelected (!row.isSelected ());
		if (row.isSelected ())
		{
			self.numCSSDsSelected (self.numCSSDsSelected () + 1);
			// add this CSSDNum to CSSDsSelected
			if (!(self.CSSDsSelected.includes (row.CSSDNum)))
			{
				self.CSSDsSelected.push (row.CSSDNum);
			}
		}
		else
		{
			self.numCSSDsSelected (self.numCSSDsSelected () - 1);
			// splice out seqNum from CSSDsSelected
			for (var i=0; i<self.CSSDsSelected.length; i++)
			{ 
			   if (self.CSSDsSelected[i]===row.CSSDNum)
			   {
				self.CSSDsSelected.splice (i, 1); 
			   }
			}
		}
	}
	self.findCSSDByName=async function (row) {
		var CSSDs=self.CSSDs ();
		for (var i=0; i<CSSDs.length; i++)
		{
			if (CSSDs[i].name===row.name)
			{
				$("#CSSD"+ CSSDs[i]._id)[0].scrollIntoView ({
					behavior: "smooth",
					block: "end"
				});

                                var parent_rect=$("#CSSD"+CSSDs[i]._id).parent()[0].getBoundingClientRect (),
				    child_rect=$("#CSSD"+CSSDs[i]._id)[0].getBoundingClientRect ();

				if ((child_rect.y-FRONTEND_Y_TOLERANCE)<parent_rect.y ||
                                    (child_rect.y+child_rect.height+FRONTEND_Y_TOLERANCE)>(parent_rect.y+parent_rect.height))
				{
                                	$("#CSSD"+ CSSDs[i]._id)[0].scrollIntoView ({
                                        	behavior: "smooth",
                                        	block: "end"
                                	});

					var num_attempts=100;
                                	while (--num_attempts)
                                	{
	                                        await sleep (FRONTEND_HIGHLIGHT_FLASH_DELAY_MS);	

						child_rect=$("#CSSD"+CSSDs[i]._id)[0].getBoundingClientRect ();

                                        	if (child_rect.y+FRONTEND_Y_TOLERANCE>=parent_rect.y &&
                                            	    (child_rect.y+child_rect.height-FRONTEND_Y_TOLERANCE)<=(parent_rect.y+parent_rect.height))
                                        	{
                                                	// child is now within visible rect (of parent)
                                                	break;
                                        	}
                                	}
				}

                                await sleep (FRONTEND_HIGHLIGHT_FLASH_DELAY_MS);	

				for (var r=0; r<FRONTEND_HIGHLIGHT_FLASH_REPEAT; r++)
				{
					CSSDs[i].isSelected (!CSSDs[i].isSelected ());
					await sleep (FRONTEND_HIGHLIGHT_FLASH_DELAY_MS);
					CSSDs[i].isSelected (!CSSDs[i].isSelected ());
					await sleep (FRONTEND_HIGHLIGHT_FLASH_DELAY_MS);
				}
				break;
			}
		}
	}
	self.toggleJobs=function (row) {
		row.isSelected (!row.isSelected ());
		if (row.isSelected ())
		{
			self.numJobsSelected (self.numJobsSelected () + 1);
			// add this jobNum to jobsSelected
			if (!(self.jobsSelected.includes (row.jobNum)))
			{
				self.jobsSelected.push (row.jobNum);
			}
		}
		else
		{
			self.numJobsSelected (self.numJobsSelected () - 1);
			// split out jobNum from jobsSelected
			for (var i=0; i<self.jobsSelected.length; i++)
			{
				if (self.jobsSelected[i]===row.jobNum)
				{
					self.jobsSelected.splice (i, 1);
				}
			}
		}
	}
	self.toggleHits=function (row) {
		row.isSelected (!row.isSelected ());
		if (row.isSelected ())
		{
			numHitsSelected (numHitsSelected () + 1);
		}
		else
		{
			numHitsSelected (numHitsSelected () - 1);
		}
	}
	self.getViewedJobId=function () {
		if (undefined!==self.viewedJob ())
			return self.viewedJob ()._id;
		else
			return undefined;
	}
	/*
	 * compute resources
	 */
	self.logs=ko.observableArray ();	
	self.lastLogMessage=ko.computed (function () {
		if (self.logs ().length>0)
		{
        		return self.logs ()[self.logs ().length-1]['message'];
        	}
        	else
        	{
        		return "";
        	}
    	}, self);
	self.pushCR=function (data) {
		self.logs.push (data);
		while (FRONTEND_CR_LOG_LIMIT<self.logs ().length) {
			self.logs.shift ();
		}
	};
	self.clearCR=function () {
		self.logs ([]);
	};
	self.CRtoString=function () {
		return self.logs ().map(function (log_entry) {
			return log_entry['time'] + FRONTEND_FIELD_SEPARATOR_CHAR + log_entry['message'];
		}).join (FRONTEND_NEWLINE_CHAR);
	}
	self.num_topics=Object.keys(FRONTEND_TOPIC).length;
	self.num_capabilities=Object.keys(FRONTEND_CAPABILITY).length;
	self.haveCapabilities=false;
	if (self.num_topics>0 && self.num_capabilities>0)
	{
		self.topicsCapabilities=[...Array(self.num_topics)].map(x=>Array(self.num_capabilities));
		for (var i=0; i<self.num_topics; i++)
		{
			for (var j=0; j<self.num_capabilities; j++)
			{
				self.topicsCapabilities[i][j]=ko.observable (false);
			}
		}
	}
	else
	{
		self.topicsCapabilities=undefined;
	}
	self.hasCapability=function (topic, capability) {
		return ko.computed (function () {
			if (FRONTEND_TOPIC.hasOwnProperty (topic) && FRONTEND_CAPABILITY.hasOwnProperty (capability))
			{
				return self.topicsCapabilities[FRONTEND_TOPIC[topic]][FRONTEND_CAPABILITY[capability]]()?true:false;
			}
			else
			{
				return false;
			}
		});
	}

	// setup event handlers
	self.updateCSSDEventHandlers ();
	self.updateJobEventHandlers ();
	self.updateHitEventHandlers ();
}

/*
 * miscellaneous animations
 */
function isCardOn (card)
{
	return !$(card).hasClass("d-none");
}

function cardAnimateOnOff (card, parentContent, siblingCard, afterFn=undefined)
{
	if (isCardOn (card))
	{
		$(card).addClass("rna-card-animate-out")
		       .one("animationend webkitAnimationEnd oAnimationEnd MSAnimationEnd", function() {
		               		$(this).removeClass("rna-card-animate-out")
		               		       .addClass("d-none");
					$(parentContent).toggleClass("d-none", siblingCard==="" || !isCardOn(siblingCard));
					if (undefined!==afterFn)
					{
						afterFn ();
					}
	                });
	}
	else
	{
		$(parentContent).removeClass("d-none");
		$(card).removeClass("d-none")
		       .addClass("rna-card-animate-in")
		       .one("animationend webkitAnimationEnd oAnimationEnd MSAnimationEnd", function() {
		       		$(this).removeClass("rna-card-animate-in");
				if (undefined!==afterFn)
				{
					afterFn ();
				}
		       	});
	}
}

function flashText (text, flashType)
{
	$(text).addClass(flashType)
		.one("animationend webkitAnimationEnd oAnimationEnd MSAnimationEnd", function() {
		       		$(this).removeClass(flashType);
	});	
}

/*
 * web sockets related
 */
async function refreshTopicsCapabilities ()  
{
        num_topics=Object.keys(FRONTEND_TOPIC).length;
        num_capabilities=Object.keys(FRONTEND_CAPABILITY).length;

	if (!num_topics || !num_capabilities)
	{
		return;
	}

	RNAkoModel.topicsCapabilities.forEach (function (t) {	// for each topic
		t.forEach (function (tc) {	// for each capability
			tc (false);	// init to false
		});
	});

	while (!isRNAwsOpen || Cookies.get('ws_slot')===undefined)
	{
		await sleep (FRONTEND_API_ATTEMPT_TIMEOUT_MS); 
	}

        if (isRNAwsOpen ())
        {
		var capSuccess=0, capFail=0, capTotal=RNAkoModel.num_capabilities*RNAkoModel.num_topics;

		$('body').addClass("wait");

		for (t in FRONTEND_TOPIC)
		{
			for (c in FRONTEND_CAPABILITY)
			{
        			try {
					$.ajax ({
					  type          : 'GET',
					  url           : "".concat (FRONTEND_PROTOCOL, FRONTEND_URI, FRONTEND_PORT?':'+FRONTEND_PORT:'', FRONTEND_URI_PATH.TC+'/'+FRONTEND_TOPIC[t]+'/'+FRONTEND_CAPABILITY[c]),
					  cache         : false,
					  contentType   : 'application/json',
					  success       : function (data, textStatus, jqXHR) {
						  		//window.tc_api_attempts=FRONTEND_TC_MAX_ATTEMPTS;
						  		try {
									var this_topic_idx=parseInt (data[FRONTEND_KEY_TOPIC]);
									var this_capability_idx=parseInt (data[FRONTEND_KEY_CAPABILITY]);
									var this_tc_success=FRONTEND_STATUS_SUCCESS===data[FRONTEND_KEY_STATUS];

						  			if (this_topic_idx>=0 && this_topic_idx<num_topics &&
									    this_capability_idx>=0 && this_capability_idx<num_capabilities)
									{
										RNAkoModel.topicsCapabilities[this_topic_idx][this_capability_idx](this_tc_success);
									}
									capSuccess++;
								} 
						  		catch (err)
						  		{
									logger ("cannot update user capabilities in UI");
									capFail++;
						  		}
							  },
					  error         : async function (xhr, error) {
								if (0<window.tc_api_attempts)
								{
									window.tc_api_attempts--;
									await sleep (FRONTEND_TC_ATTEMPT_TIMEOUT_MS); 	
									refreshTopicsCapabilities ();
								}
								else
								{
									capFail++;
									//window.tc_api_attempts=FRONTEND_TC_MAX_ATTEMPTS;
									flashText ("#computeResourcesComms", "rna-text-flash-alert-animate");
								}
							  }
					});
				} catch (e) { capFail++; } 
			}
		}

		while ((capSuccess+capFail)<capTotal)
		{
			await sleep (FRONTEND_TC_ATTEMPT_TIMEOUT_MS);
		}

		$('body').removeClass("wait");

		if (capFail===0)
		{
			RNAkoModel.haveCapabilities=true;	// now that we have all topics/caps, release functionality
		}
	}
}

function openRNAws (reconnect=false)
{
	logger ("in openRNAws");
	try
	{
		logger ("set up new RNAws");

		RNAkoModel.haveCapabilities=false;	// wait for capabilities refresh

		RNAws=new WebSocket(FRONTEND_WS_URI);

		RNAws.onopen = function(event)
		{
			logger ("in RNAws.onopen");

			logger ("in RNAws.onopen sending accessToken");

			try { RNAws.send (webLockWrapper.getAccessToken ()); } catch (ex) { }

			if (reconnect)
			{
				RNAkoModel.pushCR ({ time: currentDateTimeStr(), message: "connection re-established" });
			}
			else
			{
				RNAkoModel.pushCR ({ time: currentDateTimeStr(), message: "new connection established" });
			}

			window.sequences_api_attempts=FRONTEND_API_MAX_ATTEMPTS;
			window.jobs_api_attempts=FRONTEND_API_MAX_ATTEMPTS;
			window.job_api_attempts=FRONTEND_API_MAX_ATTEMPTS;
			window.cssds_api_attempts=FRONTEND_API_MAX_ATTEMPTS;
			window.results_api_attempts=FRONTEND_API_MAX_ATTEMPTS;
			window.tc_api_attempts=FRONTEND_API_MAX_ATTEMPTS;

			refreshTopicsCapabilities ();

			flashText ("#computeResourcesConnection", "rna-text-flash-animate");
		}
		RNAws.onerror = function(event)
		{
			RNAkoModel.pushCR ({ time: currentDateTimeStr(), message: "connection error detected" });
			flashText ("#computeResourcesComms", "rna-text-flash-alert-animate");
		}
		RNAws.onmessage = async function(event)
		{
			if (event.data[0]===FRONTEND_WS_MSG_TYPE.HEARTBEAT)
			{
				logger ("in RNAws.onmessage received HEARTBEAT");
				if (event.data.length===3)
				{
					var tmp=(event.data.charCodeAt (1) << 8) + event.data.charCodeAt (2);
					if (tmp!==RNAwsSlot)
					{
						logger ("now working with websocket slot "+tmp);

						RNAwsSlot=tmp;

						if (FRONTEND_COOKIE_SECURITY_ATTRIBUTES)
						{
							Cookies.set ('ws_slot', RNAwsSlot, FRONTEND_COOKIE_SECURITY_ATTRIBUTES);
						}
						else
						{
							Cookies.set ('ws_slot', RNAwsSlot);
						}
					}
				}
				try 
				{ 
					await sleep (FRONTEND_WS_HEARTBEAT_DELAY_MS);  // give server some breathing space between heartbeats	
					RNAws.send (FRONTEND_WS_MSG_TYPE.HEARTBEAT); 
				} catch (ex) { }
			}
			else if (event.data[0]===FRONTEND_WS_MSG_TYPE.UPDATE_NOTIFICATION)
			{
				logger ("in RNAws.onmessage received UPDATE_NOTIFICATION");

				var num_attempts=FRONTEND_TC_MAX_ATTEMPTS;
				while (num_attempts>0 && !RNAkoModel.haveCapabilities)
				{
					await sleep (FRONTEND_TC_ATTEMPT_TIMEOUT_MS);
					num_attempts--;
				}

				if (!num_attempts)
				{
					logger ("no capabilities available. cannot process update requests");
					alert ("connection to server lost");
					return;		// no capabilities -> fail
				}

				var updateEvents=parseInt (event.data.charCodeAt (1)),
                                    opType=parseInt (event.data.charCodeAt (2)),
                                    oid=parseInt (event.data.charCodeAt (3));

				ACTION++;

                                if (undefined===Object.keys (DS_OP_TYPE).find(key => DS_OP_TYPE[key]===opType))
				{
                                        console.log ("unknown update type: "+opType+". please contact administrator");
					return;
				}

                                if (oid==0)
                                {
                                	// object id is 0 (null-terminator), do the change notification
                                        // is not specific and should apply to all documents in collection
                                        oid=undefined;
                                }
                                else
                                {
                                	// object id is specified -> retrieve full string
                                        oid=event.data.substring (3, 3+24);     // DS_OBJ_ID_LENGTH===24
                                }

				[...DS_UPDATE_COLLECTION.keys ()].forEach (function (updateCollectionType) { 
					if (updateEvents & updateCollectionType)
					{
						var time_epoch=parseInt (new Date()/1),
						    last_time;

						if (undefined===oid)
						{
							DS_UPDATE_COLLECTION.get (updateCollectionType).last_update={};
							last_time=0;
						}
						else
						{
							last_time_strn=DS_UPDATE_COLLECTION.get (updateCollectionType).last_update[oid];
							if (undefined===last_time_strn)
							{
								last_time=0;
							}
							else
							{
								last_time=parseInt (last_time_strn);
							}
						}

						if (time_epoch>last_time)
						{
							var update_delay=DS_UPDATE_COLLECTION.get (updateCollectionType).update_delay;

							if (undefined!==oid)
							{
								DS_UPDATE_COLLECTION.get (updateCollectionType).last_update[oid]=time_epoch+update_delay;
							}

							if (update_delay)
							{
								// refresh collection data with given update_delay
								var fn=async function (updateCollectionType, opType, oid)
								{
									await sleep (DS_UPDATE_COLLECTION.get (updateCollectionType).update_delay); 
			
									DS_UPDATE_COLLECTION.get (updateCollectionType).action (opType, oid);

									flashText ("#computeResourcesComms", "rna-text-flash-animate");
								};

								fn (updateCollectionType, opType, oid);
							}
							else
							{
								// refresh collection immediately
								DS_UPDATE_COLLECTION.get (updateCollectionType).action (opType, oid);

                                                                flashText ("#computeResourcesComms", "rna-text-flash-animate");
							}
						}

						updateEvents &= ~updateCollectionType;
					}
				});

				if (updateEvents)
				{
					console.log ("unknown update: "+updateEvents+". please contact administrator");
				}				
			}
			else if (event.data===FRONTEND_WS_MSG_TYPE.LIMIT_REACHED)
			{
				logger ("in RNAws.onmessage received LIMIT_REACHED. setting RNAwsLimitExceeded to true");
				RNAwsLimitExceeded=true;
				RNAkoModel.pushCR ({ time: currentDateTimeStr(), message: "compute resources limit reached" });
				flashText ("#computeResourcesFail", "rna-text-flash-alert-animate");
				alert ('compute resources limit reached. please try again later');
			}
			else if (event.data===FRONTEND_WS_MSG_TYPE.CLOSE_REQUEST)
			{
				logger ("in RNAws.onmessage received CLOSE_REQUEST");
				RNAwsCloseRequested=true;
				RNAws.close ();
                                alert ("connection to server lost");
			}
			else
			{
				logger ("in RNAws.onmessage received UNKNOWN");
				flashText ("#computeResourcesComms", "rna-text-flash-alert-animate");
			}
		}
		RNAws.onclose = async function(event)
		{
			logger ("in RNAws.onclose");

			RNAkoModel.haveCapabilities=false;      // disable current capabilities set

			RNAwsSlot=0;
			Cookies.remove ('ws_slot');

			if (!RNAwsCloseRequested)
			{
				logger ("in RNAws.onclose !RNAwsCloseRequested");
				RNAkoModel.pushCR ({ time: currentDateTimeStr(), message: "connection lost" });

				flashText ("#computeResourcesConnection", "rna-text-flash-alert-animate");

				// check RNAwsLimitExceeded to avoid endless close/open loop
				if (!RNAwsLimitExceeded && RNAwsReopenAttempts)
				{
					logger ("in RNAws.onclose !RNAwsLimitExceeded calling openRNws with reconnect=true");
					await sleep (FRONTEND_WS_LOST_CONNECTION_TIMEOUT_MS);

					RNAkoModel.pushCR ({ time: currentDateTimeStr(), message: "attempting to re-establish connection" });

					RNAwsReopenAttempts--;
					openRNAws (true);

					if (!isRNAwsOpen)
					{
						logger ("in RNAws.onclose !isRNAwsOpen waiting for RNAws to open");
						await sleep (FRONTEND_WS_LOST_CONNECTION_TIMEOUT_MS);	

						if (!isRNAwsOpen)
						{
							logger ("in RNAws.onclose RNAws failed to open");
							RNAkoModel.pushCR ({ time: currentDateTimeStr(), message: "failed to re-establish connection" });
						}
					}
				}
				else
				{
					if (!RNAwsReopenAttempts)
					{
						logger ("in RNAws.onclose RNAwsReopenAttempts limit reached");
						alert ("connection to server lost");
					}
					if (RNAwsLimitExceeded)
					{
						logger ("in RNAws.onclose RNAwsLimitExceeded doing nothing");
					}
				}
			}
		}
	}
	catch (err)
	{
		logger ("in openRNAws error");
	}
}

function closeRNAws ()
{
	logger ("in closeRNAws");
	if (RNAws!==undefined && isRNAwsOpen ())
	{
		logger ("in closeRNAws RNAws is open. RNAws.send with CLOSE_REQUEST");
		// send close request to server first, then continue with close if message reciprocated
		try { RNAws.send (FRONTEND_WS_MSG_TYPE.CLOSE_REQUEST); } catch (ex) { }
	}
	else
	{
		logger ("in closeRNAws RNAws is closed. doing nothing");
	}
}

function isRNAwsOpen ()
{
	logger ("in isRNAwsOpen. state is "+(RNAws.readyState===RNAws.OPEN || RNAws.readyState===RNAws.CONNECTING || RNAws.readyState===RNAws.CLOSING));
	return RNAws.readyState===RNAws.OPEN || RNAws.readyState===RNAws.CONNECTING || RNAws.readyState===RNAws.CLOSING;
}

function submitCSSD ()
{
	var consensus=$("#cssd-table-consensus").val(),
	    posvars=$("#cssd-table-posvars").val(),
	    name=$("#cssd-table-name").val(),
	    isPublished=$("#cssd-table-availability-checkbox")[0].checked;

	if (consensus===undefined || consensus.trim().length===0)
	{
		alert ('invalid consensus field');
	}
	else if (posvars===undefined || posvars.length>consensus.length)
	{
		alert ('positional variables field length exceeds that of the consensus');
	}
	else if (name===undefined || name.trim().length===0)
	{
		alert ('invalid CSSD name field');
	}
	else
	{
                $('body').addClass("wait");

		var cssd;
		if (undefined===posvars || posvars.length===0)
			cssd=consensus;
		else
			cssd=consensus+FRONTEND_NEWLINE_CHAR+posvars; 
		$.ajax ({ 
			type 		: 'POST',
			url 	  	: "".concat (FRONTEND_PROTOCOL, FRONTEND_URI, FRONTEND_PORT?':'+FRONTEND_PORT:'', FRONTEND_URI_PATH.CSSD),
			contentType	: 'application/json',
			data 		: JSON.stringify( {
						"cs"		: cssd,
						"name" 		: name,
						"ws_slot" 	: RNAwsSlot, 
						"published"	: isPublished,
						"token" 	: webLockWrapper.getAccessToken () } ),
			success		: function (data, textStatus, jqXHR) {
						flashText ("#computeResourcesComms", "rna-text-flash-animate");
						$('#CSSDModal').modal('hide');
					  },
			error		: function (xhr, error) {
                                                var err_msg=xhr.responseJSON;
                                                if (err_msg!==undefined && err_msg[FRONTEND_KEY_ERROR_MSG]!==undefined && err_msg[FRONTEND_KEY_ERROR_MSG].length>0)
                                                {
                                                        alert ('failed to submit CSSD'+FRONTEND_NEWLINE_CHAR+'[ '+err_msg[FRONTEND_KEY_ERROR_MSG]+' ]');
                                                }
                                                else
                                                {
                                                        alert ('failed to submit CSSD');
                                                }

						flashText ("#computeResourcesComms", "rna-text-flash-alert-animate");
					  }
		});

                $('body').removeClass("wait");
	}
}

function modifyCSSD ()
{
        var consensus=$("#cssd-table-consensus").val(),
            posvars=$("#cssd-table-posvars").val(),
            name=$("#cssd-table-name").val(),
            isPublished=$("#cssd-table-availability-checkbox")[0].checked,
	    id=RNAkoModel.getCSSDId (RNAkoModel.getCSSDsSelected ()[0]);

        if (consensus===undefined || consensus.trim().length===0)
        {
                alert ('invalid consensus field');
        }
        else if (posvars===undefined || posvars.length>consensus.length)
        {
                alert ('positional variables field length exceeds that of the consensus');
        }
        else if (name===undefined || name.trim().length===0)
        {
                alert ('invalid CSSD name field');
        }
        else
        {
                $('body').addClass("wait");

                var cssd;
                if (undefined===posvars || posvars.length===0)
                        cssd=consensus;
                else
                        cssd=consensus+FRONTEND_NEWLINE_CHAR+posvars;
                $.ajax ({
                        type            : 'PUT',
                        url             : "".concat (FRONTEND_PROTOCOL, FRONTEND_URI, FRONTEND_PORT?':'+FRONTEND_PORT:'', FRONTEND_URI_PATH.CSSD),
                        contentType     : 'application/json',
                        data            : JSON.stringify( {
						"_id"		: id,
                                                "cs"            : cssd,
                                                "name"          : name,
                                                "ws_slot"       : RNAwsSlot,
                                                "published"     : isPublished,
                                                "token"         : webLockWrapper.getAccessToken () } ),
                        success         : function (data, textStatus, jqXHR) {
                                                flashText ("#computeResourcesComms", "rna-text-flash-animate");
                                                $('#CSSDModal').modal('hide');
                                          },
                        error           : function (xhr, error) {
                                                alert ('failed to modify CSSD');
                                                flashText ("#computeResourcesComms", "rna-text-flash-alert-animate");
                                          }
                });

                $('body').removeClass("wait");
        }
}

function deleteCSSD (e)
{
        e.preventDefault ();

        var num_cssd_selected=RNAkoModel.getNumCSSDsSelected ();

        if (1!=num_cssd_selected)
        {
                alert ('1 CSSD required');
        }
        else
        {
                $('body').addClass("wait");

                var i=0, cssds=RNAkoModel.CSSDs (), cssdSelected=RNAkoModel.CSSDsSelected;
		$.ajax ({
			type            : 'DELETE',
			url             : "".concat (FRONTEND_PROTOCOL, FRONTEND_URI, FRONTEND_PORT?':'+FRONTEND_PORT:'', FRONTEND_URI_PATH.CSSD+'/'+cssds [cssdSelected[i]-1]._id),
			contentType     : 'application/json',
			cache           : false,
			success         : function (data, textStatus, jqXHR) {
							flashText ("#computeResourcesComms", "rna-text-flash-animate");
					  },
			error           : function (xhr, error) {
							flashText ("#computeResourcesComms", "rna-text-flash-animate");
							alert ('could not delete CSSD');
					  }
		});

                $('body').removeClass("wait");
        }
}

function newSequence ()
{
	var accession=$("#new-sequence-table-accession").val(),
	    definition=$("#new-sequence-table-definition").val(),
	    seqnt=$("#new-sequence-table-sequence").val(),
	    group=$("#new-sequence-table-group").val();

	if (accession.length===0 || definition.length===0 || seqnt.length===0 || group.length===0)
	{
		alert ('all fields are required');
	}
	else
	{
                $('body').addClass("wait");

		$.ajax ({ 
			type 		: 'POST',
			url 	  	: "".concat (FRONTEND_PROTOCOL, FRONTEND_URI, FRONTEND_PORT?':'+FRONTEND_PORT:'', FRONTEND_URI_PATH.SEQUENCE),
			contentType	: 'application/json',
			data 		: JSON.stringify( {
						"accession"	: accession.trim(),
						"definition" 	: definition.trim(),
						// auto-replace 't' with 'u' + remove white space + lowercase
						"3\'UTR" 	: seqnt.replace(/\s/g, '').replace(/[0-9]/g, '').toLowerCase().replace (/t/g, 'u'),
						"group" 	: group.trim(),
						"ws_slot" 	: RNAwsSlot, 
						"token" 	: webLockWrapper.getAccessToken () } ),
			success		: function (data, textStatus, jqXHR) {
						flashText ("#computeResourcesComms", "rna-text-flash-animate");
						$('#NewSequenceModal').modal('hide');
						RNAkoModel.pushCR ({ time: currentDateTimeStr(), message: "new sequence with accession \""+accession+"\" created" });
					  },
			error		: function (xhr, error) {
				    		var err_msg=xhr.responseJSON; 
						if (err_msg!==undefined && err_msg[FRONTEND_KEY_ERROR_MSG]!==undefined && err_msg[FRONTEND_KEY_ERROR_MSG].length>0)
						{
							alert ('failed to create sequence'+FRONTEND_NEWLINE_CHAR+'[ '+err_msg[FRONTEND_KEY_ERROR_MSG]+' ]');
						}
						else
						{
							alert ('failed to create sequence'+FRONTEND_NEWLINE_CHAR+'[ ensure that accession, definition, and sequence are unique ]');
						}

						flashText ("#computeResourcesComms", "rna-text-flash-alert-animate");
					  }
		});

                $('body').removeClass("wait");
	}
}

async function deleteSequences (e) 
{
	e.preventDefault ();

	var num_seq_selected=RNAkoModel.getNumSequencesSelected ();

	if (1>num_seq_selected)
	{
		alert ('1 or more sequences required');
	}
	else
	{
                $('body').addClass("wait");

		var i=0, num_seq_deleted=0, sequences=RNAkoModel.sequences (), sequencesSelected=RNAkoModel.sequencesSelected, num_seq_done=0;
		for (; i<num_seq_selected; i++)
		{
			$.ajax ({ 
				type 		: 'DELETE',
				url 	  	: "".concat (FRONTEND_PROTOCOL, FRONTEND_URI, FRONTEND_PORT?':'+FRONTEND_PORT:'', FRONTEND_URI_PATH.SEQUENCE+'/'+sequences [sequencesSelected[i]-1]._id),
				contentType	: 'application/json',
				cache		: false,
				success		: function (data, textStatus, jqXHR) {
							num_seq_deleted++;
							num_seq_done++;
						  },
				error		: function (xhr, error) {
							num_seq_done++;
						  }
			});
		}
		while (num_seq_done<num_seq_selected)
		{
			await sleep (FRONTEND_API_ATTEMPT_TIMEOUT_MS); 
		}

                $('body').removeClass("wait");

		if (num_seq_deleted===num_seq_selected)
		{
			flashText ("#computeResourcesComms", "rna-text-flash-animate");
		}
		else
		{
			flashText ("#computeResourcesComms", "rna-text-flash-alert-animate");
			alert (num_seq_deleted+' of '+num_seq_selected+' sequences deleted');
		}
		RNAkoModel.pushCR ({ time: currentDateTimeStr(), message: num_seq_deleted+' of '+num_seq_selected+' sequences deleted' });
	}
}

async function deleteJobsWithHits (e)  
{
	e.preventDefault ();

	var num_jobs_selected=RNAkoModel.getNumJobsSelected ();

	if (1>num_jobs_selected)
	{
		alert ('1 or more jobs required');
	}
	else
	{
                $('body').addClass("wait");

		var i=0, num_jobs_deleted=0, jobs=RNAkoModel.jobs (), jobsSelected=RNAkoModel.getJobsSelected (), num_jobs_done=0;
		for (; i<num_jobs_selected; i++)
		{
			$.ajax ({ 
				type 		: 'DELETE',
				url 	  	: "".concat (FRONTEND_PROTOCOL, FRONTEND_URI, FRONTEND_PORT?':'+FRONTEND_PORT:'', FRONTEND_URI_PATH.JOB_W_RESULTS+'/'+
						  jobs[jobsSelected [i]-1]._id),
				contentType	: 'application/json',
				cache		: false,
				success		: function (data, textStatus, jqXHR) {
							num_jobs_deleted++;
							num_jobs_done++;
						  },
				error		: function (xhr, error) {
							num_jobs_done++;
						  }
			});
		}
		while (num_jobs_done<num_jobs_selected)
		{
			await sleep (FRONTEND_API_ATTEMPT_TIMEOUT_MS); 
		}

		RNAkoModel.clearJobsSelected ();
		// update job (row) numbers
		var jobNum=1;
		ko.utils.arrayForEach (RNAkoModel.jobs(), function (j) {
                	j.jobNum=jobNum++;
                });

                $('body').removeClass("wait");

		if (num_jobs_deleted===num_jobs_selected)
		{
			flashText ("#computeResourcesComms", "rna-text-flash-animate");
		}
		else
		{
			flashText ("#computeResourcesComms", "rna-text-flash-alert-animate");
			alert (num_jobs_deleted+' of '+num_jobs_selected+' jobs deleted');
		}
		RNAkoModel.pushCR ({ time: currentDateTimeStr(), message: num_jobs_deleted+' of '+num_jobs_selected+' jobs deleted' });
	}
}

async function runSearch (e)
{
	if (undefined!==e)
	{
		e.preventDefault ();
	}

	var num_seq_selected=RNAkoModel.getNumSequencesSelected (),
	    num_CSSD_selected=RNAkoModel.getNumCSSDsSelected (), num_submitted=0, num_failed=0;

	if (1>num_seq_selected || 1>num_CSSD_selected)
	{
		alert ("1 or more sequences and CSSDs required");
	}
	else
	{
		var CSSDsSelected=RNAkoModel.getCSSDsSelected (),
		    sequencesSelected=RNAkoModel.getSequencesSelected (),
		    CSSDId=RNAkoModel.getCSSDId (RNAkoModel.getCSSDsSelected ()[0]);

		RNAkoModel.pushCR ({ time: currentDateTimeStr(), message: 
					'submitting ' +
					sequencesSelected.length + ' sequence' + (sequencesSelected.length>1 ? 's':'') + ' against ' +
					CSSDsSelected.length + ' CSSD' + (CSSDsSelected.length>1 ? 's':'') + ' to search' });

                $('body').addClass("wait");

		for (c in CSSDsSelected)
		{
			var CSSDId=RNAkoModel.getCSSDId (CSSDsSelected[c]);

			for (s in sequencesSelected) 
			{
				var sequenceId=RNAkoModel.getSequenceId (sequencesSelected[s]),
				    sequenceAccession=RNAkoModel.getSequenceAttributeFromId (sequenceId, 'accession'),
				    sequenceDefinition=RNAkoModel.getSequenceAttributeFromId (sequenceId, 'definition'),
				    CSSDName=RNAkoModel.getCSSDAttributeFromId (CSSDId, 'name').trunc (50);

                		RNAkoModel.pushCR ({ time: currentDateTimeStr(), message:
                                        'submitted sequence (' + sequenceAccession + ', '+ sequenceDefinition.trunc (30) + ') against CSSD (' + CSSDName + ') to search' });

				$.ajax ({
					type            : 'POST',
					url             : "".concat (FRONTEND_PROTOCOL, FRONTEND_URI, FRONTEND_PORT?':'+FRONTEND_PORT:'', FRONTEND_URI_PATH.JOB),
					contentType     : 'application/json',
					data            : JSON.stringify ({
								"cssd_id" 	: CSSDId, 
								"sequence_id" 	: sequenceId, 
								"ws_slot" 	: RNAwsSlot, 
								"token" 	: webLockWrapper.getAccessToken () 
							  }),
					success		: function (data, textStatus, jqXHR) {
								num_submitted++;
							  },
					error		: function (jqXHR, textStatus, errorThrown) {
								num_failed++;
							  }
				});
			}
		}

		// wait until status of all submitted queries is known
		while ((num_seq_selected*num_CSSD_selected)>(num_submitted+num_failed))
		{
			await sleep (FRONTEND_QUERY_WAIT_MS);
		}

                $('body').removeClass("wait");

		if ((num_seq_selected*num_CSSD_selected)>num_submitted)
		{
			alert ("failed to submit "+((num_seq_selected*num_CSSD_selected)-num_submitted)+" of "+(num_seq_selected*num_CSSD_selected)+" queries");
		}
	}
}

async function restartConnection ()
{
	if (RNAws!==undefined)
	{
		RNAkoModel.pushCR ({ time: currentDateTimeStr(), message: "connection close request made" });

		if (RNAws!==undefined && isRNAwsOpen ())
		{
			RNAwsCloseRequested=true;
			closeRNAws ();

			num_attempts=FRONTEND_WS_MAX_RESTART_ATTEMPTS;
			while (true)
			{
				if (isRNAwsOpen ())
				{
					if (0<num_attempts--)
					{
						await sleep (FRONTEND_WS_CLOSE_ATTEMPT_TIMEOUT_MS);
					}
					else
					{
						break;
					}
					
				}
				else
				{
					break;
				}
			}

			RNAkoModel.pushCR ({ time: currentDateTimeStr(), message: "connection closed" });
			RNAwsCloseRequested=false;
		}

		RNAwsLimitExceeded=false;
		openRNAws ();
	}
}

function RNAsetup ()
{
	// when initiatiating a new connection, set RNAwsLimitExceeded to false. RNAwsLimitExceeded
	// will be set to true if server sends FRONTEND_WS_MSG_TYPE.LIMIT_REACHED. required
	// to avoid endless loop of close/open when trying to recover from lost connections
	RNAwsLimitExceeded=false;

	// clear websocket cookie
	Cookies.remove ('ws_slot');

        /*
         * knockout bindings
         */

	// create and register new template engine for SVG rendering
	// (from https://groups.google.com/forum/#!msg/knockoutjs/P2WqnOtnTqo/U5lURjaXENgJ) 
	var SVGTemplateEngine = function () {
	    this['allowTemplateRewriting']=false;
	}

	SVGTemplateEngine.prototype=new ko.templateEngine();
	SVGTemplateEngine.prototype.renderTemplateSource=function (templateSource, bindingContext, options) {
	    var nodes = templateSource.nodes();
	    if (nodes)
	    {
		return nodes;
	    }
	    var div=document.createElement('div');
	    var text='<svg xmlns="http://www.w3.org/2000/svg">'+templateSource.text()+'</svg>';
	    div.innerHTML = text;
	    return ko.utils.arrayPushAll ([], div.childNodes[0].childNodes);
	};

	window.SVGTemplateEngine=new SVGTemplateEngine();

	// setup ko and apply bindings
	ko.options.deferUpdates=true; 
        RNAkoModel=new koModel ();
        RNAkoModel.pushCR ({ time: currentDateTimeStr(), message: "browser session initiated" });
        ko.applyBindings (RNAkoModel);

	/*
	 * miscellaneous UI/event handlers
	 *
	 * note: for sequenceGroups and job event handling also see refreshSequences and refreshJobs
	 */

	/*
	 * setup focus and key handling for Modals
	 */
	$('#SelectSequencesBySearchModal').on('shown.bs.modal', function() {
		RNAkoModel.isShowingModal=true;
		$('#select-sequences-by-search-table-term').focus ();
		$('#select-sequences-by-search-table-term').select();
	});
	$('#SelectCSSDsBySearchModal').on('shown.bs.modal', function() {
                $('#select-CSSDs-by-search-table-term').focus ();
		$('#select-CSSDs-by-search-table-term').select ();
        });
	$('#SelectJobsBySearchModal').on('shown.bs.modal', function() {
                $('#select-jobs-by-search-table-term').focus ();
		$('#select-jobs-by-search-table-term').select ();
        });
        $('#NewSequenceModal').on('shown.bs.modal', function() {
                $('#new-sequence-table-accession').focus ();
		$('#new-sequence-table-accession').select ();
        });
	$('#CSSDModal').on('shown.bs.modal', function() {
                $('#new-cssd-table-consensus').focus ();
		$('#new-cssd-table-consensus').select ();
        });
	$('#StatsModal').on('shown.bs.modal', function() {
		RNAkoModel.isShowingStatsModal=true;
	});

	$('.modal').on('show.bs.modal', function() {
                RNAkoModel.isShowingModal=true;
        });
        $('.modal').on('hide.bs.modal', function() {
                RNAkoModel.isShowingModal=false;
		RNAkoModel.isShowingStatsModal=false;
        });

	// submit/ok click/key handling; note that ENTER key event is ignored when button already has focus so as to avoid "double" click/activation
	$("#SelectSequencesBySearchModal").on("keypress", function(e) {
   		if (13===e.which && !$("#select-sequences-by-search-accept").is(":focus")) { RNAkoModel.selectSequencesBySearchTerm (); } 
	});
	$("#select-sequences-by-search-accept").on ('click', RNAkoModel.selectSequencesBySearchTerm);
        $("#SelectCSSDsBySearchModal").on("keypress", function(e) {
                if (13===e.which && !$("#select-CSSDs-by-search-accept").is(":focus")) { RNAkoModel.selectCSSDsBySearchTerm (); }
        });
        $("#select-CSSDs-by-search-accept").on ('click', RNAkoModel.selectCSSDsBySearchTerm);
	$("#SelectJobsBySearchModal").on("keypress", function(e) {
                if (13===e.which && !$("#select-jobs-by-search-accept").is(":focus")) { RNAkoModel.selectJobsBySearchTerm (); }
        });
        $("#select-jobs-by-search-accept").on ('click', RNAkoModel.selectJobsBySearchTerm);
	$("#NewSequenceModal").on("keypress", function(e) {
                if (13===e.which && !$("#new-sequence-accept").is(":focus")) { newSequence (); }
        });
        $("#new-sequence-accept").on ('click', newSequence); 

	$("#navbar-CSSDs-new").on ('click', function () { 
		$("#cssd-table-consensus")[0].value="";
		$("#cssd-table-posvars")[0].value="";
		$("#cssd-table-name")[0].value="";
		$("#cssd-table-availability-checkbox")[0].checked=0;
		$("#cssd-accept")[0].innerText="Submit";
		$('#CSSDModal').modal('show');
	});
	$("#navbar-CSSDs-edit").on ('click', function () { 
		var num_CSSD_selected=RNAkoModel.getNumCSSDsSelected ();

		if (num_CSSD_selected!==1)
		{
			alert ('1 CSSD required');
		}
		else
		{
			var CSSDSelected=RNAkoModel.getCSSDsSelected ()[0],
			    re_ws=new RegExp (FRONTEND_WHITEPSACE,"g"),
			    complete_cs=RNAkoModel.getCSSDcs (CSSDSelected).replace (FRONTEND_CSSD_STRING_SEPARATOR_HTML_PREFIX, '').replace (re_ws, ' '),
			    cs="",
			    posvars="",
			    cs_split_posn=complete_cs.indexOf (FRONTEND_CSSD_STRING_SEPARATOR_HTML_SUFFIX);

			if (cs_split_posn!==-1)
			{
				cs=complete_cs.substring (0, cs_split_posn);
				posvars=complete_cs.substring (cs_split_posn+FRONTEND_CSSD_STRING_SEPARATOR_HTML_SUFFIX.length).replace (FRONTEND_CSSD_STRING_SEPARATOR_HTML_SUFFIX, "");
			}
			else
			{
				cs=complete_cs;
			}

			$("#cssd-table-consensus")[0].value=cs;
			$("#cssd-table-posvars")[0].value=posvars;
			$("#cssd-table-name")[0].value=RNAkoModel.getCSSDName (CSSDSelected);
			$("#cssd-table-availability-checkbox")[0].checked=RNAkoModel.isCSSDpublished (CSSDSelected);
			$("#cssd-accept")[0].innerText="Modify";
			$('#CSSDModal').modal('show');
		}
	});

	// key bindings
	Mousetrap.bind ({
		'?'	: function () { $("#searchHelpModal").modal ('show'); },
		'alt+r' : function () { runSearch (undefined); },
  		'alt+s' : function () { $("#SelectSequencesBySearchModal").modal ('show'); },
		'alt+c' : function () { $("#SelectCSSDsBySearchModal").modal ('show'); },
		'alt+j' : function () { $("#SelectJobsBySearchModal").modal ('show'); },
		'alt+l' : function () { $('#computerResourcesLogModal').modal('show'); },
		'esc'	: function () { RNAkoModel.scrollIntoViewEnabled=!(RNAkoModel.scrollIntoViewEnabled); }
	});

	/*
	 * other event handling
	 */
	$("#navbar-sequences-delete").on ('click', deleteSequences);
	$("#cssd-accept").on ('click', function () {
		if ($("#cssd-accept")[0].innerText==="Submit")
		{
			submitCSSD();
		}
		else if ($("#cssd-accept")[0].innerText==="Modify")
		{
			modifyCSSD();
		}
	});
	$("#navbar-CSSDs-delete").on ('click', deleteCSSD);
	$("#navbar-jobs-delete").on ('click', deleteJobsWithHits);
	$("#navbar-jobs-download").on ('click', downloadJobHitData);
	$("#navbar-order-dropdown-item-position").click (function (e) {
		e.preventDefault();
		if ("pos"!==hitOrder ())
		{
			hitOrder ("pos");
			refreshViewedJobHitData ();
			refreshViewedJobHitStats ();
		}
	});
	$("#navbar-order-dropdown-item-energy").click (function (e) {
		e.preventDefault();
		if ("fe"!==hitOrder ())
		{
			hitOrder ("fe");
			refreshViewedJobHitData ();
			refreshViewedJobHitStats ();
		}
	});
	$(".chooseCSSDToggle").mousedown(function(e) {
		e.preventDefault();
		cardAnimateOnOff ("#chooseCSSDsCard", "#CSSDSequencesContent", "#selectSequencesCard");
	});
	$(".selectSequencesToggle").mousedown(function(e) {
		e.preventDefault();
		cardAnimateOnOff ("#selectSequencesCard", "#CSSDSequencesContent", "#chooseCSSDsCard");
	});
	$(".viewJobsToggle").mousedown(function(e) {
		e.preventDefault();

		cardAnimateOnOff ("#viewJobsCard", "#JobsContent", "", function () {
			RNAkoModel.setViewingJobs (!($("#viewJobsCard").hasClass ("d-none")));
		});

		if ($("#viewJobsCard").hasClass ("d-none"))
		{
			var viewedJobId=RNAkoModel.getViewedJobId ();
			if (viewedJobId!==undefined)
			{
				var parent_rect=undefined, child_rect=undefined;

				parent_rect=$("#job"+viewedJobId).parent()[0].getBoundingClientRect (),
				child_rect=$("#job"+viewedJobId)[0].getBoundingClientRect ();

				if (child_rect.y-FRONTEND_Y_TOLERANCE<parent_rect.y ||
				    (child_rect.y+child_rect.height+FRONTEND_Y_TOLERANCE)>(parent_rect.y+parent_rect.height))
				{
					$("#job"+viewedJobId)[0].scrollIntoView ({
						behavior: "auto",
						block: "nearest"
					});
				}
			}
		}
	});
	hoverAnimateTarget ("#computeResourcesContainer", "#computeResourcesLastLog");

	$("#computeResourcesContainer").mousedown (
	    function() {
		$('#computerResourcesLogModal').modal('show');
	    }
	);
	$("#computerResourcesRestartButton").mousedown(function (e) {
		e.preventDefault ();
		restartConnection ();
	});
	$("#computerResourcesClearButton").mousedown (function (e) {
		e.preventDefault ();
		RNAkoModel.clearCR ();
	});

	/*
	 * auth0/lock
	 */
	hoverAnimateTarget ("#accessProfileRegister", "#accessProfileAssist",
				function () {
					$("#accessProfileAssist").text ("register a new profile");
				},
				function () {
					$("#accessProfileAssist").text ("");
				}, "rna-text-opacity-animate-in-fast", "rna-text-opacity-animate-out-fast");

	$("#accessProfileRegister").on ('mousedown', function (e) {
		e.preventDefault();
		webLockWrapper.authenticateShow (AUTH0_LOCK_SIGNUP_OPTIONS);
	});

	// trigger single callback when authentication state changes
	webLockWrapper.setCallback (function (isAuthenticated) {

		if (0<=isAuthenticated)
		{
			logger ("in callback with isAuthenticated "+isAuthenticated);
			if (isAuthenticated==1)
			{
				// authenticated user
				$("#accessProfileLoginIcon").removeClass("rna-feather-nav-medium").addClass("rna-feather-nav-medium-disabled");
				$("#accessProfileGuestIcon").addClass("rna-feather-nav-medium").removeClass("rna-feather-nav-medium-disabled");

				hoverAnimateTarget ("#accessProfileLogin", "#accessProfileAssist",
							function () {
								$("#accessProfileAssist").text (webLockWrapper.getUserName());
							},
							function () {
								$("#accessProfileAssist").text ("");
							}, "rna-text-opacity-animate-in-fast", "rna-text-opacity-animate-out-fast");
				hoverAnimateTarget ("#accessProfileGuest", "#accessProfileAssist",
							function () {
								$("#accessProfileAssist").text ("switch to guest profile");
							},
							function () {
								$("#accessProfileAssist").text ("");
							}, "rna-text-opacity-animate-in-fast", "rna-text-opacity-animate-out-fast");

				$("#accessProfileLogin").off ('mousedown');
				$("#accessProfileGuest").on ('mousedown', function (e) {
					e.preventDefault();
					webLockWrapper.logout ();
				});

				if (webLockWrapper.getUserName()!==undefined)
				{
					RNAkoModel.pushCR ({ time: currentDateTimeStr(), message: "logged in as "+webLockWrapper.getUserName() });
				}
			}
			else
			{
				// guest access
				$("#accessProfileLoginIcon").addClass("rna-feather-nav-medium").removeClass("rna-feather-nav-medium-disabled");
				$("#accessProfileGuestIcon").removeClass("rna-feather-nav-medium").addClass("rna-feather-nav-medium-disabled");

				hoverAnimateTarget ("#accessProfileLogin", "#accessProfileAssist",
							function () {
								$("#accessProfileAssist").text ("login with your profile");
							},
							function () {
								$("#accessProfileAssist").text ("");
							}, "rna-text-opacity-animate-in-fast", "rna-text-opacity-animate-out-fast");
				hoverAnimateTarget ("#accessProfileGuest", "#accessProfileAssist",
							function () {
								$("#accessProfileAssist").text ("now using a guest profile");
							},
							function () {
								$("#accessProfileAssist").text ("");
							}, "rna-text-opacity-animate-in-fast", "rna-text-opacity-animate-out-fast");

				$("#accessProfileGuest").off ('mousedown');
				$("#accessProfileLogin").on ('mousedown', function (e) {
					e.preventDefault();
					webLockWrapper.authenticateShow (AUTH0_LOCK_LOGIN_OPTIONS);
				});

				RNAkoModel.pushCR ({ time: currentDateTimeStr(), message: "logged in as guest" });
			}
			logger ("in callback setting RNAwsCloseRequested to true");
			RNAwsCloseRequested=true;
			logger ("in callback calling closeRNAws");
			closeRNAws ();
			logger ("in callback calling openRNAws");
			openRNAws ();
			logger ("in callback setting RNAwsCloseRequested to false");
			RNAwsCloseRequested=false;
		}
		else
		{
			logger ("in callback with fail status. doing nothing");
		}
	});

	/*
	 * ideally key event handling is done through knockout, however, currently
	 * only input based elements seem to be able to negotiate such events
	 */
	$(document).keydown (RNAkoModel.keydownHandler);
	$("#searchLink").mousedown (runSearch);

	/*
	 * enable bootstrap tooltips
	 */
	$(document).ready (function(){
  		$('[data-toggle="tooltip"]').tooltip();
	});
}

/*
 * auth0 related
 */
// adapted from https://github.com/auth0/auth0.js
var webLockWrapper=(function () {
	/*
	 * private vars
	 */
	// var idToken;
	var accessToken=AUTH0_GUEST_ACCESS_TOKEN;
	var expiresAt;
	var callback;
	var webLock;
	var self=this;

	if (FRONTEND_COOKIE_SECURITY_ATTRIBUTES)
	{
		Cookies.set ('token', accessToken, FRONTEND_COOKIE_SECURITY_ATTRIBUTES);
	}
	else
	{
		Cookies.set ('token', accessToken);
	}

	/*
	 * private functions
	 */
	self.webLock=new Auth0Lock(
		AUTH0_CLIENT_ID,
		AUTH0_DOMAIN,
		AUTH0_LOCK_LOGIN_OPTIONS
	);

	self.pendingAuthorization=function (isPending)
	{
		localStorage.setItem ('isRNAPendingAuthorization', isPending);
		if (isPending)
		{
			localStorage.setItem ('RNAPendingAuthorizationTS', new Date().getTime());
		}
	}

	self.isPendingAuthorization=function ()
	{
		if (localStorage.getItem ('isRNAPendingAuthorization')==='true')
		{
			var nw=new Date().getTime();
			if (FRONTEND_WS_PENDING_AUTHORIZATION_TIMEOUT_MS > new Date().getTime()-parseInt(localStorage.getItem ('RNAPendingAuthorizationTS')))
			{
				return true;
			}
			else
			{
				// timeout
				localStorage.setItem ('isRNAPendingAuthorization', false);
			}
		}
		else
		{
			return false;
		}
	}

	function getAccessToken ()
	{
		return accessToken;
	}

	function setAccessToken (newAccessToken)
	{
		accessToken=newAccessToken;
	}

	function localLogin (authResult, user)
	{
		logger ("in localLogin for user "+user.name);
		localStorage.setItem('isRNAUserLoggedIn', 'true');
		localStorage.setItem('RNAUser', user.name);
		expiresAt=JSON.stringify (authResult.expiresIn * 1000 + new Date().getTime());
		accessToken=authResult.accessToken;
	}

	function renewTokens ()
	{
		self.webLock.checkSession(AUTH0_AUTH, (err, authResult) => {
			if (authResult && authResult.accessToken /*&& authResult.idToken*/)
			{
				logger ("in renewTokens with authResult ok. calling getUserInfo");				
				// equivalent of:
				// wget -qO- --header="Authorization: Bearer SEgfstgZa8mODY0XtJEqeF-Tm0kuuwXs" https://rna.eu.auth0.com/userinfo

				self.webLock.getUserInfo (authResult.accessToken, function(err, user) {
					if (user && user.name)
					{
						logger ("in renewTokens and getUserInfo with user "+user.name+" and wrapper.lastUser "+wrapper.lastUser);
						if (user.name!==wrapper.lastUser)
						{
							logger ("in renewTokens and getUserInfo calling localLogin");
							wrapper.lastUser=user.name;
							localLogin (authResult, user);
							flashText ("#accessProfileLoginIcon", "rna-text-flash-animate");
							logger ("in renewTokens and getUserInfo calling callback (1)");
							self.callback (1);
						}
						else if (authResult.accesstoken!==getAccessToken ())
						{
							setAccessToken (authResult.accessToken);

							RNAws.send (FRONTEND_WS_MSG_TYPE.ACCESS_TOKEN_REFRESH+FRONTEND_WS_MSG_ACCESS_TOKEN_REFRESH_DELIMITER+authResult.accessToken);
						}

						if (FRONTEND_COOKIE_SECURITY_ATTRIBUTES)
						{
							Cookies.set ('token', authResult.accessToken, FRONTEND_COOKIE_SECURITY_ATTRIBUTES);
						}
						else
						{
							Cookies.set ('token', authResult.accessToken);
						}
					}
					else
					{
						setAccessToken (AUTH0_GUEST_ACCESS_TOKEN);

						if (FRONTEND_COOKIE_SECURITY_ATTRIBUTES)
						{
							Cookies.set ('token', AUTH0_GUEST_ACCESS_TOKEN, FRONTEND_COOKIE_SECURITY_ATTRIBUTES);
						}
						else
						{
							Cookies.set ('token', AUTH0_GUEST_ACCESS_TOKEN);
						}

						logger ("in renewTokens and getUserInfo with no user returned");
						flashText ("#accessProfileLoginIcon", "rna-text-flash-alert-animate");
						logger ("in renewTokens and getUserInfo calling callback (-1)");
						self.callback (-1);
					}
				});
			}
			else if (err)
			{
				logger ("in renewTokens with err");
				flashText ("#accessProfileLoginIcon", "rna-text-flash-alert-animate");
				wrapper.logout();
				logger ("in renewTokens calling callback (-1)");
				self.callback (-1);
			}
			else
			{
				logger ("in renewTokens with token expired");
				logger ("in renewTokens calling callback (0)");
				self.callback (0);
			}
		});
	}

	/*
	 * wrapper interface
	 */
	var wrapper=new Object();

	wrapper.lastUser='';

    	self.webLock.on ("authenticated", function (authResult) {

		logger ("in webLock authenticated event");
		self.pendingAuthorization (true);

		if (authResult && authResult.accessToken /*&& authResult.idToken*/)
		{
			logger ("in webLock authenticated event with authResult ok. calling getUserInfo");

			self.webLock.getUserInfo (authResult.accessToken, function(err, user) {
				if (user && user.name)
				{
					logger ("in webLock authenticated event for user.name "+user.name+" and wrapper.lastUer "+wrapper.lastUser);
					if (user.name!==wrapper.lastUser)
					{
						logger ("in webLock authenticated event setting lastUser to "+user.name);
						wrapper.lastUser=user.name;
						logger ("in webLock authenticated event calling localLogin");
						localLogin (authResult, user);
						flashText ("#accessProfileLoginIcon", "rna-text-flash-animate");
						logger ("in webLock authenticated event calling callback (1)");
						self.callback (1);
						self.pendingAuthorization (false);
					}
					else
					{
						self.pendingAuthorization (false);
					}

					if (FRONTEND_COOKIE_SECURITY_ATTRIBUTES)
					{
						Cookies.set ('token', authResult.accessToken, FRONTEND_COOKIE_SECURITY_ATTRIBUTES);
					}
					else
					{
						Cookies.set ('token', authResult.accessToken);
					}
				}
				else
				{
					setAccessToken (AUTH0_GUEST_ACCESS_TOKEN);

					if (FRONTEND_COOKIE_SECURITY_ATTRIBUTES)
					{
						Cookies.set ('token', AUTH0_GUEST_ACCESS_TOKEN, FRONTEND_COOKIE_SECURITY_ATTRIBUTES);
					}
					else
					{
						Cookies.set ('token', AUTH0_GUEST_ACCESS_TOKEN);
					}

					logger ("in webLock authenticated event for no user");
					flashText ("#accessProfileLoginIcon", "rna-text-flash-alert-animate");
					logger ("in webLock authenticated event calling callback (-1)");
					self.callback (-1);
					self.pendingAuthorization (false);
				}
			});
		}
		else
		{
			logger ("in webLock authenticated event with no authResult");
			logger ("in webLock authenticated event calling callback (0)");
			self.callback (0);
			self.pendingAuthorization (false);
		}
	});
	
	wrapper.getAccessToken=function ()
	{
		return accessToken;
	}

	wrapper.setCallback=function (cb)
	{
		self.callback=cb;
	}

	wrapper.getUserName=function ()
	{
		var user=localStorage.getItem ('RNAUser');
		if (!user)
		{
			return undefined;
		}
		return user;
	}

	wrapper.logout=async function ()	
	{
		if (isCardOn ("#selectSequencesCard"))
		{
			cardAnimateOnOff ("#selectSequencesCard", "#CSSDSequencesContent", "#chooseCSSDsCard");
		}
		if (isCardOn ("#chooseCSSDsCard"))
		{
			cardAnimateOnOff ("#chooseCSSDsCard", "#CSSDSequencesContent", "#selectSequencesCard");
		}
		if (isCardOn ("#viewJobsCard"))
		{
			cardAnimateOnOff ("#viewJobsCard", "#JobsContent", "", function () {
				RNAkoModel.setViewingJobs (!($("#viewJobsCard").hasClass ("d-none")));
			});
		}

		logger ("in logout calling closeRNAws");
		RNAwsCloseRequested=true;
                closeRNAws ();   // close existing ws

		var oldUser=localStorage.getItem ('RNAUser');
		localStorage.removeItem ('isRNAUserLoggedIn');
		localStorage.removeItem ('RNAUser');
		accessToken=AUTH0_GUEST_ACCESS_TOKEN;

		if (FRONTEND_COOKIE_SECURITY_ATTRIBUTES)
		{
			Cookies.set ('token', accessToken, FRONTEND_COOKIE_SECURITY_ATTRIBUTES);
		}
		else
		{
			Cookies.set ('token', accessToken);
		}

		// idToken = '';
		expiresAt = 0;

		logger ("in wrapper.webLock.logout with oldUser "+oldUser);

		logger ("in wrapper.webLock.logout calling webLock.logout()");
		self.webLock.logout (AUTH0_LOGOUT_OPTION);

		logger ("in login calling openRNAws");
                RNAwsCloseRequested=false;

		await sleep (FRONTEND_WS_CLOSE_ATTEMPT_TIMEOUT_MS);  // give some time for server to cleanup

                openRNAws ();   // open new  ws
	}

	wrapper.isAuthenticated=function ()
	{
		var expiration=parseInt (expiresAt) || 0;

		logger ("in wrapper.isAuthenticated with status "+localStorage.getItem('isRNAUserLoggedIn')==='true' && new Date().getTime()<expiration);

		return localStorage.getItem('isRNAUserLoggedIn')==='true' && new Date().getTime()<expiration;
	}

	wrapper.authenticate=function ()
	{
		logger ("in wrapper.authenticate with isRNAUserLoggedIn "+localStorage.getItem('isRNAUserLoggedIn'));
		if (localStorage.getItem('isRNAUserLoggedIn')==='true')
		{
			logger ("in wrapper.authenticate calling renewTokens()");
			renewTokens();
		}
		else
		{
			logger ("in wrapper.authenticate calling callback(0)");
			self.callback (0);
		}
	}

	wrapper.authenticateShow=function (options)
	{
		logger ("in wrapper.authenticateShow. calling webLock.show");
		self.webLock.show (options);
	}

	window.addEventListener ('load', async function () {

		logger ('in window.load waiting for pendingAuthorization '+isPendingAuthorization ());

		while (isPendingAuthorization ())
		{
			await sleep (FRONTEND_WS_PENDING_AUTHORIZATION_WAIT_MS);
		}

		logger ("in window.load calling wrapper.authenticate");
		wrapper.authenticate ();
	});

	return wrapper;
})();

/*
 * miscellaneous tools
 */
function pad (n, width, z) {
	// https://stackoverflow.com/questions/10073699/pad-a-number-with-leading-zeros-in-javascript/10073737	
	z = z || '0';
	n = n + '';
	return n.length >= width ? n : new Array(width - n.length + 1).join(z) + n;
}

function sleep (ms) {
  	return new Promise(resolve => setTimeout(resolve, ms));
}

function isStringArray (what) {
	return Object.prototype.toString.call ($.parseJSON (what))==='[object Array]';
}

function currentDateTimeStr ()
{
	cd=new Date();
	return cd.getFullYear() + "." + pad ((parseInt (cd.getMonth())+1), 2) + "." + pad (cd.getDate(), 2) + ' ' + 
		pad (cd.getHours(), 2) + ":" + pad (cd.getMinutes(), 2) + ":" + pad (cd.getSeconds(), 2);
}

function hoverAnimateTarget (hoverElementId, targetElementId,
			     beforeHoverFunc, afterHoverFunc,
			     animateInClass="rna-text-opacity-animate-in", animateOutClass="rna-text-opacity-animate-out",
			     animateSustainClass="rna-text-nav-small-high-profile")
{
	$(hoverElementId).hover (
	    function () {
	    	if (beforeHoverFunc!==undefined)
	    	{
	    		beforeHoverFunc ();
	    	}
		$(targetElementId).addClass (animateInClass)
		       .one("animationend webkitAnimationEnd oAnimationEnd MSAnimationEnd", function() {
		               		$(this).removeClass (animateInClass)
		               		       .addClass (animateSustainClass);
	                });
    	    }
    	    ,
	    function () {
	        $(targetElementId).removeClass (animateSustainClass);
		$(targetElementId).addClass (animateOutClass)
		       .one("animationend webkitAnimationEnd oAnimationEnd MSAnimationEnd", function() {
		               		$(this).removeClass (animateOutClass);
				    	if (afterHoverFunc!==undefined)
				    	{
				    		afterHoverFunc ();
				    	}
	                });
	    }
	);
}

function logger (msg)
{
	if (loggerToConsole)
	{
		console.log (msg);
	}
}

/*
 * data helper functions
 */
function refreshSequencesData (opType, oid)
{
        /*
         * this query is not specific to objects, given the low-frequency upate and typcially small-to-medium size dataset; just update all
         */
	if (isRNAwsOpen ())
	{
                $('body').addClass("wait");

		try {
			$.ajax ({ 
			  type 		: 'GET', 
			  url 		: "".concat (FRONTEND_PROTOCOL, FRONTEND_URI, FRONTEND_PORT?':'+FRONTEND_PORT:'', FRONTEND_URI_PATH.SEQUENCES+'/1/0/1/none'),
			  cache 	: false,
			  contentType	: 'application/json',
			  success	: function (data, textStatus, jqXHR) {
			  			//window.sequences_api_attempts=FRONTEND_API_MAX_ATTEMPTS;
			  			RNAkoModel.refreshSequences (JSON.parse (data));
						flashText ("#computeResourcesComms", "rna-text-flash-animate");
			  		  },
			  error		: async function (xhr, error) {
		  				if (0<window.sequences_api_attempts)
		  				{
							window.sequences_api_attempts--;
		  					await sleep (FRONTEND_API_ATTEMPT_TIMEOUT_MS);	
		  					refreshSequencesData (opType, oid);
		  				}
		  				else
		  				{
							//window.sequences_api_attempts=FRONTEND_API_MAX_ATTEMPTS;
				  			RNAkoModel.refreshSequences ([]);
							flashText ("#computeResourcesComms", "rna-text-flash-alert-animate");
		  				}
			  		  }
			});
		} catch (e) {
			// no output to js console
		}

                $('body').removeClass("wait");
	}
}

function refreshCSSDsData (opType, oid)
{
	/*
	 * this query is not specific to objects, given the low-frequency upate and typcially small dataset; just update all
	 */
	if (isRNAwsOpen ())
	{
                $('body').addClass("wait");

		try {
			$.ajax ({ 
			  type 		: 'GET',
			  url 		: "".concat (FRONTEND_PROTOCOL, FRONTEND_URI, FRONTEND_PORT?':'+FRONTEND_PORT:'', FRONTEND_URI_PATH.CSSDS+'/1/0/1/none'),
			  cache 	: false,
			  contentType	: 'application/json',
			  success	: function (data, textStatus, jqXHR) {
				  		//window.cssds_api_attempts=FRONTEND_API_MAX_ATTEMPTS;
			  			RNAkoModel.refreshCSSDs (JSON.parse (data));
						flashText ("#computeResourcesComms", "rna-text-flash-animate");
			  		  },
			  error		: async function (xhr, error) {
		  				if (0<window.cssds_api_attempts)
		  				{
							window.cssds_api_attempts--;
		  					await sleep (FRONTEND_API_ATTEMPT_TIMEOUT_MS);
		  					refreshCSSDsData (opType, oid);
		  				}
		  				else
		  				{
							//window.cssds_api_attempts=FRONTEND_API_MAX_ATTEMPTS;
				  			RNAkoModel.refreshCSSDs ([]);
							flashText ("#computeResourcesComms", "rna-text-flash-alert-animate");
		  				}
			  		  }
			});
		} catch (e) {
			// no output to js console
		}

                $('body').removeClass("wait");
	}
}

function refreshJobsData (opType, oid)
{
	if (isRNAwsOpen ())
	{
                $('body').addClass("wait");

		if (undefined===oid)
		{
			/*
			 * job data is unspecific (no object id provided), so update all jobs
			 */
			try {
				$.ajax ({ 
				  type 		: 'GET',
				  url 		: "".concat (FRONTEND_PROTOCOL, FRONTEND_URI, FRONTEND_PORT?':'+FRONTEND_PORT:'', FRONTEND_URI_PATH.JOBS+'/1/0/1/none'),
				  cache 	: false,
				  contentType	: 'application/json',
				  success	: function (data, textStatus, jqXHR) {
					  		//window.jobs_api_attempts=FRONTEND_API_MAX_ATTEMPTS;
							RNAkoModel.refreshJobs (JSON.parse (data));
							flashText ("#computeResourcesComms", "rna-text-flash-animate");
						  },
				  error		: async function (xhr, error) {
							if (0<window.jobs_api_attempts)
							{
								window.jobs_api_attempts--;
								await sleep (FRONTEND_API_ATTEMPT_TIMEOUT_MS);
								refreshJobsData (opType, oid);
							}
							else
							{
								//window.jobs_api_attempts--;
								RNAkoModel.refreshJobs ([]);
								flashText ("#computeResourcesComms", "rna-text-flash-alert-animate");
							}
						  }
				});
			} catch (e) {
			}
		}
		else
		{	
			if (DS_OP_TYPE['DELETE']===opType)
			{
				// delete the specific job
				var jobs=RNAkoModel.jobs (), jobsLength=jobs.length; 

				if (jobsLength)
				{
					for (var j=0; j<jobsLength; j++)
					{
						if (jobs[j]._id===oid)
						{
							RNAkoModel.jobs.remove (jobs[j]);
							break;
						}
					}

                			RNAkoModel.viewedJob (undefined);
                			RNAkoModel.clearJobsSelected ();
					RNAkoModel.hitPage (0);
					RNAkoModel.hitIndex (0);
					RNAkoModel.viewedHit (undefined);
					refreshViewedJobHitStats ();
				}
			}
			else
			{
				// update/insert specific job with given id
				try {
					$.ajax ({
					  type          : 'GET',
					  url           : "".concat (FRONTEND_PROTOCOL, FRONTEND_URI, FRONTEND_PORT?':'+FRONTEND_PORT:'', FRONTEND_URI_PATH.JOB+'/'+oid),
					  cache         : false,
					  contentType   : 'application/json',
					  success       : function (data, textStatus, jqXHR) {
						  		//window.job_api_attempts=FRONTEND_API_MAX_ATTEMPTS;
								RNAkoModel.refreshJob (JSON.parse (data));
								flashText ("#computeResourcesComms", "rna-text-flash-animate");
							  },
					  error         : async function (xhr, error) {
								if (0<window.job_api_attempts)
								{
									window.job_api_attempts--;
									await sleep (FRONTEND_API_ATTEMPT_TIMEOUT_MS);
									refreshJobsData (opType, oid);
								}
								else
								{
									//window.job_api_attempts=FRONTEND_API_MAX_ATTEMPTS;
									RNAkoModel.refreshJobs ([]);
									flashText ("#computeResourcesComms", "rna-text-flash-alert-animate");
								}
							  }
					});
				} catch (e) {
				}
			}
		}

                $('body').removeClass("wait");
	}
}

function refreshViewedJobHitCount ()
{
        if (undefined!==RNAkoModel.viewedJob () && undefined!==RNAkoModel.viewedJob ()._id && 0<RNAkoModel.viewedJob ()._id.length && isRNAwsOpen ())
        {
                $('body').addClass("wait");

                try {
                        $.ajax ({
                          type          : 'GET',
                          url           : "".concat (FRONTEND_PROTOCOL, FRONTEND_URI, FRONTEND_PORT?':'+FRONTEND_PORT:'', FRONTEND_URI_PATH.RESULTS+'/', RNAkoModel.viewedJob ()._id),
                          cache         : false,
                          contentType   : 'application/json',
                          success       : function (data, textStatus, jqXHR) {
				  		//window.results_api_attempts=FRONTEND_API_MAX_ATTEMPTS;
				  		try {
							if (data!==undefined && FRONTEND_STATUS_SUCCESS===data[FRONTEND_KEY_STATUS] && undefined!==data[FRONTEND_KEY_COUNT])
							{
								viewedJobNumHits (data[FRONTEND_KEY_COUNT]);

                                                		flashText ("#computeResourcesComms", "rna-text-flash-animate");
								return;
							}
						} catch (e) {};

						viewedJobNumHits (-1);
                                                flashText ("#computeResourcesComms", "rna-text-flash-animate");
                                          },
                          error         : async function (xhr, error) {
                                                if (0<window.results_api_attempts)
                                                {
							window.results_api_attempts--;
                                                        await sleep (FRONTEND_API_ATTEMPT_TIMEOUT_MS);
                                                        refreshViewedJobHitCount ();
                                                }
                                                else
                                                {
							//window.results_api_attempts--;
							viewedJobNumHits (-1);
                                                        flashText ("#computeResourcesComms", "rna-text-flash-alert-animate");
                                                }
                                          }
                        });
                } catch (e) {
                        // no output to js console
                }

                $('body').removeClass("wait");
        }
        else
        {
		viewedJobNumHits (-1);
                flashText ("#computeResourcesComms", "rna-text-flash-animate");
        }
}

function refreshJobHitCount (jobId, hitCount)
{
        if (undefined!==jobId && 0<jobId.length && isRNAwsOpen ())
        {
                $('body').addClass("wait");

                try {
                        $.ajax ({
                          type          : 'GET',
                          url           : "".concat (FRONTEND_PROTOCOL, FRONTEND_URI, FRONTEND_PORT?':'+FRONTEND_PORT:'', FRONTEND_URI_PATH.RESULTS+'/', jobId),
                          cache         : false,
                          contentType   : 'application/json',
                          success       : function (data, textStatus, jqXHR) {
				  		//window.results_api_attempts=FRONTEND_API_MAX_ATTEMPTS;

				  		try {
							if (FRONTEND_STATUS_SUCCESS===data[FRONTEND_KEY_STATUS] &&
							    0<=parseInt (data[FRONTEND_KEY_COUNT]))
							{
								hitCount (parseInt (data[FRONTEND_KEY_COUNT]));
							}
						} catch (e) {};

                                                flashText ("#computeResourcesComms", "rna-text-flash-animate");
                                          },
                          error         : async function (xhr, error) {
                                                if (0<window.results_api_attempts)
                                                {
							window.results_api_attempts--;
                                                        await sleep (FRONTEND_API_ATTEMPT_TIMEOUT_MS);

                                                        refreshJobHitCount (jobId, hitCount);
                                                }
                                                else
                                                {
							//window.results_api_attempts--;
                                                        flashText ("#computeResourcesComms", "rna-text-flash-alert-animate");
                                                }
                                          }
                        });
                } catch (e) { }

                $('body').removeClass("wait");
        }
        else
        {
                flashText ("#computeResourcesComms", "rna-text-flash-animate");
        }
}

function refreshJobHitTime (jobId, hitTime)
{
        if (undefined!==jobId && 0<jobId.length && isRNAwsOpen ())
        {
                $('body').addClass("wait");

                try {
                        $.ajax ({
                          type          : 'GET',
                          url           : "".concat (FRONTEND_PROTOCOL, FRONTEND_URI, FRONTEND_PORT?':'+FRONTEND_PORT:'', FRONTEND_URI_PATH.RESULT+'/', jobId),
                          cache         : false,
                          contentType   : 'application/json',
                          success       : function (data, textStatus, jqXHR) {
				  		//window.results_api_attempts=FRONTEND_API_MAX_ATTEMPTS;

				  		try {
							if (FRONTEND_STATUS_SUCCESS===data[FRONTEND_KEY_STATUS] &&
							    0.0<=parseFloat (data[FRONTEND_KEY_TIME]))
							{
								hitTime (parseFloat (data[FRONTEND_KEY_TIME]).toFixed (4));
							}
						} catch (e) {};

                                                flashText ("#computeResourcesComms", "rna-text-flash-animate");
                                          },
                          error         : async function (xhr, error) {
                                                if (0<window.results_api_attempts)
                                                {
							window.results_api_attempts--;
                                                        await sleep (FRONTEND_API_ATTEMPT_TIMEOUT_MS);

                                                        refreshJobHitTime (jobId, hitTime);
                                                }
                                                else
                                                {
							//window.results_api_attempts--;
                                                        flashText ("#computeResourcesComms", "rna-text-flash-alert-animate");
                                                }
                                          }
                        });
                } catch (e) { }

                $('body').removeClass("wait");
        }
        else
        {
                flashText ("#computeResourcesComms", "rna-text-flash-animate");
        }
}

function refreshJobsHit_CountTimeStats (opType, oid)
{
	var jobs=RNAkoModel.jobs ();
	for (var i=0; i<jobs.length; i++)
	{
		if (jobs[i]._id===oid)
		{
                	refreshJobHitCount (jobs[i]._id, jobs[i].hit_count);
			refreshJobHitTime (jobs[i]._id, jobs[i].hit_time);
			if (RNAkoModel.getViewedJobId ()===oid)
			{
				// if oid is the currently viewed job, update the hit/results data as well
				refreshViewedJobHitCount ();
				refreshViewedJobHitData ();
				refreshViewedJobHitStats ();
			}
			return;
		}
	}
}

function refreshViewedJobHitData (startFrom=undefined)
{
	if (undefined!==RNAkoModel.getViewedJobId () && isRNAwsOpen ())
	{
                $('body').addClass("wait");

		// first get update on hits
		try {
			$.ajax ({ 
			  type 		: 'GET',
			  url 		: "".concat (FRONTEND_PROTOCOL, FRONTEND_URI, FRONTEND_PORT?':'+FRONTEND_PORT:'', FRONTEND_URI_PATH.RESULTS+'/')+
			  			(undefined===startFrom ? '1/' : (startFrom+'/'))+
			  			hitLimit ()+'/'+
			  			("fe"===hitOrder () ? "fe" : "position") +'/'+
		  				'1/'+
		  				'none/'+
		  				RNAkoModel.getViewedJobId (),
			  cache 	: false,
			  contentType	: 'application/json',
			  success	: function (data, textStatus, jqXHR) {
				  		//window.results_api_attempts=FRONTEND_API_MAX_ATTEMPTS;

			  			RNAkoModel.refreshHits (JSON.parse (data));
						flashText ("#computeResourcesComms", "rna-text-flash-animate");
			  		  },
			  error		: async function (xhr, error) {
		  				if (0<window.results_api_attempts)
		  				{
							window.results_api_attempts--;
		  					await sleep (FRONTEND_API_ATTEMPT_TIMEOUT_MS);
		  					refreshViewedJobHitData (startFrom);
		  				}
		  				else
		  				{
							//window.results_api_attempts--;
				  			RNAkoModel.refreshHits ([]);
							flashText ("#computeResourcesComms", "rna-text-flash-alert-animate");
		  				}
			  		  }
			});
		} catch (e) {
			// no output to js console
		}

                $('body').removeClass("wait");
	}
	else
	{
		RNAkoModel.refreshHits ([]);
		flashText ("#computeResourcesComms", "rna-text-flash-animate");
	}
}

function getViewedJobHitIndex (position, fe)
{
	if (undefined!==RNAkoModel.getViewedJobId () && isRNAwsOpen ())
	{
                $('body').addClass("wait");

		try {
			$.ajax ({ 
			  type 		: 'GET', 
			  url 		: "".concat (FRONTEND_PROTOCOL, FRONTEND_URI, FRONTEND_PORT?':'+FRONTEND_PORT:'', FRONTEND_URI_PATH.RESULT_INDEX+'/')+
			  			("fe"===hitOrder () ? "fe" : "position") +'/'+
		  				'1/'+
		  				RNAkoModel.getViewedJobId ()+'/'+
						position+'/'+
						fe+'/',
			  cache 	: false,
			  contentType	: 'application/json',
			  success	: async function (data, textStatus, jqXHR) {
				  		//window.results_api_attempts=FRONTEND_API_MAX_ATTEMPTS;

						var ht=hitLimit (),
                                                    nh=viewedJobNumHits (),
					  	    hitIndex=Math.min (Math.max (0, JSON.parse (data).index), nh-1),
					  	    page=Math.floor (hitIndex/ht),
					  	    index=hitIndex-(page*ht);

                                                RNAkoModel.hitPage (page);
                                                RNAkoModel.hitIndex (0);

				  		refreshViewedJobHitData (1+(page*ht));
                                                refreshViewedJobHitStats ();

				  		await sleep (FRONTEND_HIT_SEEK_MS);

				  		var ok=false, num_attempts=FRONTEND_HIT_SEEK_MAX_ATTEMPTS; 
				  		while (!ok && num_attempts>0) {
							var hits=RNAkoModel.hits();
							if (hits!==undefined && hits.length>0) {
								if ("fe"===hitOrder ()) {
									for (var i=0; i<hits.length; i++) {
										var thisPosition=parseInt (hits[i].position);
										if (thisPosition==position) {
											ok=true;
											break;
										}
									}
								} else {
									var targetFE=fe.toFixed (2);
                                                                        for (var i=0; i<hits.length; i++) {
                                                                                var thisFE=parseFloat (hits[i].fe).toFixed (2), diff;
										if (thisFE>targetFE || ((targetFE-thisFE)<0.01)) {
											ok=true;
											break;
										}
                                                                        }
								}
							}

				  			await sleep (FRONTEND_HIT_SEEK_MS);
							num_attempts--;
						}

				  		if (ok) {
							RNAkoModel.viewedHit (hits[index]);
							RNAkoModel.hitIndex (index);
						}

						flashText ("#computeResourcesComms", "rna-text-flash-animate");
			  		  },
			  error		: async function (xhr, error) {
		  				if (0<window.results_api_attempts)
		  				{
							window.results_api_attempts--;
		  					await sleep (FRONTEND_API_ATTEMPT_TIMEOUT_MS);
		  					getViewedJobHitIndex (position, fe);
		  				}
		  				else
		  				{
							//window.results_api_attempts--;
				  			RNAkoModel.refreshHits ([]);
							flashText ("#computeResourcesComms", "rna-text-flash-alert-animate");
		  				}
			  		  }
			});
		} catch (e) {
			// no output to js console
		}

                $('body').removeClass("wait");
	}
	else
	{
		RNAkoModel.refreshHits ([]);
		flashText ("#computeResourcesComms", "rna-text-flash-animate");
	}
}

function refreshViewedJobHitStats ()
{
	if (undefined!==RNAkoModel.getViewedJobId () && isRNAwsOpen ())
	{
                $('body').addClass("wait");

		var jobId=RNAkoModel.getViewedJobId ();
                try {
                        $.ajax ({
                          type          : 'GET',
                          url           : "".concat (FRONTEND_PROTOCOL, FRONTEND_URI, FRONTEND_PORT?':'+FRONTEND_PORT:'', FRONTEND_URI_PATH.RESULTS_SUMMARY+'/')+jobId,
                          cache         : false,
                          contentType   : 'application/json',
                          success       : function (data, textStatus, jqXHR) {
				  		//window.results_api_attempts=FRONTEND_API_MAX_ATTEMPTS;

				  		RNAkoModel.refreshViewedJobHitStats (jobId, JSON.parse (data));
                                                flashText ("#computeResourcesComms", "rna-text-flash-animate");
                                          },
                          error         : async function (xhr, error) {
                                                if (0<window.results_api_attempts)
                                                {
							window.results_api_attempts--;
                                                        await sleep (FRONTEND_API_ATTEMPT_TIMEOUT_MS);
                                                        refreshViewedJobHitStats ();
                                                }
                                                else
                                                {
							//window.results_api_attempts--;
                                                        flashText ("#computeResourcesComms", "rna-text-flash-alert-animate");
                                                }
                                          }
                        });
                } catch (e) {
                        // no output to js console
                }

                $('body').removeClass("wait");
	}
	else
	{
		$("#statsButton").hide ();
	}
}

function retryFailedJob (failed_job_id)
{
	if (undefined!==failed_job_id && failed_job_id.length===49)		// failed_job_id===sequence_id + ':' separator + cssd_id (in HTML)
	{
                $('body').addClass("wait");

		var sequenceId=failed_job_id.substring (0,24),
		    CSSDId=failed_job_id.substring (25);

		$.ajax ({
			type            : 'POST',
			url             : "".concat (FRONTEND_PROTOCOL, FRONTEND_URI, FRONTEND_PORT?':'+FRONTEND_PORT:'', FRONTEND_URI_PATH.JOB),
			contentType     : 'application/json',
			data            : JSON.stringify ({
						"cssd_id"       : CSSDId,
						"sequence_id"   : sequenceId,
						"ws_slot"       : RNAwsSlot,
						"token"         : webLockWrapper.getAccessToken ()
					  }),
			success         : function (data, textStatus, jqXHR) {
					  },
			error           : function (jqXHR, textStatus, errorThrown) {
						alert ('failed to retry job');
					  }
		});

                $('body').removeClass("wait");
	}
}

async function downloadJobHitData ()
{
	var jobs=RNAkoModel.jobs (),
	    sequences=RNAkoModel.sequences (),
	    jobsSelected=RNAkoModel.jobsSelected,
	    jobsDownloaded=0,
	    jobsFailed=0,
	    downloadData=[];

	if (RNAkoModel.numJobsSelected && undefined!==jobsSelected && 0<jobsSelected.length && undefined!==jobs && jobs.length>=jobsSelected.length && isRNAwsOpen ())
	{
                $('body').addClass("wait");

		for (var j=0; j<jobsSelected.length; j++)
		{
			try {
				$.ajax ({ 
				  type 		: 'GET',
				  url 		: "".concat (FRONTEND_PROTOCOL, FRONTEND_URI, FRONTEND_PORT?':'+FRONTEND_PORT:'', FRONTEND_URI_PATH.RESULTS+'/')+
							'1/'+
							'0/'+
							("fe"===hitOrder () ? "fe" : "position") +'/'+
							'1/'+
							'none/'+
							jobs[jobsSelected[j]-1]._id,
				  cache 	: false,
				  contentType	: 'application/json',
				  context	: jobs[jobsSelected[j]-1],
				  success	: function (data, textStatus, jqXHR) {
					  		jobsDownloaded+=1;

					  		var seqnt=undefined;
					  		for (var s=0; s<sequences.length; s++)
					  		{
								if (sequences[s].definition===this.definition)
								{
									downloadData.push ("#"+FRONTEND_FIELD_SEPARATOR_CHAR+"Sequence accession"+FRONTEND_FIELD_SEPARATOR_CHAR+sequences[s].accession+FRONTEND_NEWLINE_CHAR);
									if (undefined!==sequences[s].full_nt)
									{
										seqnt=sequences[s].full_nt;
									}
									else
									{
										seqnt=sequences[s].seqnt;
									}
									break;
								}
							}
					  		downloadData.push ("#"+FRONTEND_FIELD_SEPARATOR_CHAR+"Sequence definition"+FRONTEND_FIELD_SEPARATOR_CHAR+this.definition+FRONTEND_NEWLINE_CHAR);
					  		downloadData.push ("#"+FRONTEND_FIELD_SEPARATOR_CHAR+"CSSD name"+FRONTEND_FIELD_SEPARATOR_CHAR+this.name+FRONTEND_NEWLINE_CHAR);
					  		downloadData.push ("#"+FRONTEND_FIELD_SEPARATOR_CHAR+"Job status"+FRONTEND_FIELD_SEPARATOR_CHAR+this.status ()+FRONTEND_NEWLINE_CHAR);
					  		downloadData.push ("#"+FRONTEND_FIELD_SEPARATOR_CHAR+"Hit count"+FRONTEND_FIELD_SEPARATOR_CHAR+this.hit_count ()+FRONTEND_NEWLINE_CHAR);
							var hits=JSON.parse (data);
					  		for (var h=0; h<hits.length; h++)
					  		{
								downloadData.push ((h+1)+FRONTEND_FIELD_SEPARATOR_CHAR+hits[h].position+FRONTEND_NEWLINE_CHAR);
								downloadData.push ((h+1)+FRONTEND_FIELD_SEPARATOR_CHAR+hits[h].fe+FRONTEND_NEWLINE_CHAR);
								downloadData.push ((h+1)+FRONTEND_FIELD_SEPARATOR_CHAR+hits[h].hit_string+FRONTEND_NEWLINE_CHAR);
								if (undefined!==seqnt)
								{
									downloadData.push ((h+1)+FRONTEND_FIELD_SEPARATOR_CHAR+
											   seqnt.substring (parseInt (hits[h].position)-1, (parseInt (hits[h].position)+(hits[h].hit_string.length)-1))+FRONTEND_NEWLINE_CHAR);
								}
							}
							flashText ("#computeResourcesComms", "rna-text-flash-animate");
						  },
				  error		: async function (xhr, error) {
					  		jobsFailed+=1;
							flashText ("#computeResourcesComms", "rna-text-flash-alert-animate");
						  }
				});
			} catch (e) {
				// no output to js console
			}
		}

		while (jobsDownloaded+jobsFailed<jobsSelected.length)
		{
                	await sleep (FRONTEND_API_ATTEMPT_TIMEOUT_MS);
		}

                $('body').removeClass("wait");

		var blob = new Blob(downloadData, {type: "text/plain;charset=utf-8"});
		saveAs (blob, "SRHS.txt");

		if (0<jobsFailed)
		{
			alert (jobsDownloaded+" of "+jobsSelected.length+" jobs downloaded")
		}
	}
	else
	{
                alert ("1 or more Jobs required");
	}
}

RNAsetup ();
