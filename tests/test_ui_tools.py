import json
import importlib.util
from pathlib import Path
import unittest
import sys

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('check_ui', ROOT / 'tools/input/check_ui.py')
ui = importlib.util.module_from_spec(spec)
spec.loader.exec_module(ui)
sys.path.insert(0,str(ROOT/'tools/input'))
import check_hud as hud
import check_hover as hover
import check_scanout as scanout
import check_fade as fade


class UiEvidenceTest(unittest.TestCase):
    def test_fade_gate_rejects_preblank_noise_and_ignores_coverage_alpha(self):
        black=bytes((0,0,0,255))*(320*240)
        rows=[dict(kind='vi',field=100,blank=True,regs=[2,0,320])]
        images={}
        for field in (95,96,97):
            name=f'{field}.rgba'; images[name]=black
            rows.append(dict(kind='upload',field=field,task=field,rgba=name,width=320,height=240))
        self.assertTrue(fade.check(rows,images)['terminal_fade_black_accepted'])
        corrupt=bytearray(black); corrupt[-8]=8; images['97.rgba']=bytes(corrupt)
        self.assertFalse(fade.check(rows,images)['terminal_fade_black_accepted'])
        images['97.rgba']=black
        self.assertFalse(fade.check(rows[:3],images)['terminal_fade_black_accepted'])
        self.assertFalse(fade.check(rows[1:],images)['terminal_fade_black_accepted'])
        images['97.rgba']=black[:-1]
        with self.assertRaises(ValueError): fade.check(rows,images)

    def test_scanout_requires_blank_and_matching_resumed_generation(self):
        regs=[2,0x100000+1280,640,0,0,0,0,0,0,(108<<16)|748,(37<<16)|511,0,1024,2048]
        rows=[dict(kind='vi',revision=1,field=100,blank=True,regs=regs,task=4,minimum_task=5),
            dict(kind='present',revision=1,field=100,visible=False,task=0,address=0,width=0),
            dict(kind='vi',revision=2,field=102,blank=False,regs=regs,task=5,minimum_task=5),
            dict(kind='present',revision=2,field=102,visible=True,task=5,address=0x100000,width=640)]
        self.assertTrue(scanout.check(rows)['scanout_accepted'])
        rows[-1]['task']=4
        self.assertFalse(scanout.check(rows)['scanout_accepted'])
        rows[-1]['task']=6
        self.assertFalse(scanout.check(rows)['scanout_accepted'])
        rows[-1]['task']=5; rows[-1]['address']+=1
        self.assertFalse(scanout.check(rows)['scanout_accepted'])
        rows[-1]['address']=0x100000; rows[-1]['width']=320
        self.assertFalse(scanout.check(rows)['scanout_accepted'])
        rows[-1]['width']=640; rows[1]['visible']=True
        self.assertFalse(scanout.check(rows)['scanout_accepted'])
        self.assertFalse(scanout.check(rows[-2:])['scanout_accepted'])
        rows[1]['visible']=False; rows[-1].update(cpu=True,task=4)  # swap after task 4
        self.assertTrue(scanout.check(rows)['scanout_accepted'])
        rows[-1]['task']=3                                        # before the transition
        self.assertFalse(scanout.check(rows)['scanout_accepted'])

    def message_rows(self):
        return [dict(kind='click',at=100,mode='message',token=7,queued=True,source_field=99,x=160,y=120),
            dict(kind='pad',at=160,ui_mode='message',ui_token=7,buttons=0x8000),
            dict(kind='info',at=200,ui_mode='game')]

    def test_message_requires_its_own_confirmation_and_return(self):
        rows = self.message_rows()
        self.assertTrue(ui.check(rows, [])['gates']['message'])
        self.assertFalse(ui.check(rows[:2], [])['gates']['message'])
        rows[1]['ui_token'] = 8
        self.assertFalse(ui.check(rows, [])['gates']['message'])

    def test_stale_or_unqueued_click_is_not_live_acceptance(self):
        rows = self.message_rows()
        rows[0]['source_field'] = 70
        self.assertFalse(ui.check(rows, [])['gates']['message'])
        rows[0]['source_field'] = 99
        rows[0]['queued'] = False
        self.assertFalse(ui.check(rows, [])['gates']['message'])

    def test_later_click_cannot_supply_missing_confirmation(self):
        rows = self.message_rows()
        rows.insert(1, dict(kind='click',at=150,mode='message',token=7,queued=False,source_field=149,x=160,y=120))
        self.assertFalse(ui.check(rows, [])['gates']['message'])

    def test_keyboard_start_cannot_certify_briefing_click(self):
        rows = [dict(kind='click',at=100,mode='briefing',token=263,queued=True,source_field=99,x=320,y=400),
            dict(kind='pad',at=101,ui_mode='briefing',buttons=0x1000),
            dict(kind='info',at=500,ui_mode='game')]
        self.assertTrue(ui.check(rows, [])['gates']['briefing'])
        self.assertFalse(ui.check(rows, [dict(field=400,buttons=0x1000)])['gates']['briefing'])

    def test_text_briefing_click_is_a(self):
        rows = [dict(kind='click',at=100,mode='briefing',token=263,queued=True,source_field=99,x=320,y=400),
            dict(kind='pad',at=101,ui_mode='briefing',buttons=0x8000),
            dict(kind='info',at=500,ui_mode='game')]
        self.assertTrue(ui.check(rows, [])['gates']['briefing'])
        rows[1]['buttons'] = 0x4000
        self.assertFalse(ui.check(rows, [])['gates']['briefing'])

    def test_message_proof_is_neither_physical_input_nor_whole_gate(self):
        report = ui.check(self.message_rows(), [])
        self.assertFalse(report['physical_input_accepted'])
        self.assertFalse(report['mission_accepted'])
        self.assertFalse(report['ui_recipe_accepted'])

    def test_engine_error_rejects_evidence(self):
        with self.assertRaises(ValueError):
            ui.observations(b'ERROR:SCRIPT: failed\n')

    def test_parser_rejects_candidate_and_incomplete_pad_telemetry(self):
        with self.assertRaises(ValueError):
            ui.observations(b'SC64 gesture {"buttons":2,"field":100}\n')
        with self.assertRaises(ValueError):
            ui.observations(b'SC64 pad {"buttons":2,"field":100}\n')
        rows=[]
        for player in (1,2):
            rows.append('SC64 pad '+json.dumps(dict(stage='submitted',player=player,
                source=dict(controller=True),buttons=32768,field=100,x=0,y=0)))
        parsed=ui.observations(('\n'.join(rows)).encode())
        self.assertEqual(len(parsed),1)
        self.assertEqual(parsed[0]['buttons'],32768)
        self.assertEqual(parsed[0]['kind'],'pad')

    def test_hint_requires_visible_matching_mask_zero_stick_and_release(self):
        item=dict(id=1,group=4,context=123,label=2,buttons=2,x0=195,y0=178,x1=240,y1=195)
        rows=[dict(kind='click',at=100,mode='game',token=3,queued=True,source_field=99,x=220,y=185),
            dict(kind='pad',at=101,ui_mode='game',ui_token=3,ui_field=100,buttons=2,x=0,y=0,ui_items=[item]),
            dict(kind='pad',at=106,buttons=0,x=0,y=0)]
        self.assertTrue(hud.check(rows,[])['hint_controller_accepted'])
        self.assertFalse(hud.check(rows[:2],[])['hint_controller_accepted'])
        rows[1]['x']=.2
        self.assertFalse(hud.check(rows,[])['hint_controller_accepted'])
        rows[1]['x']=0; rows[1]['buttons']=32768
        self.assertFalse(hud.check(rows,[])['hint_controller_accepted'])

    def test_build_replay_accepts_menu_pulse_spacing(self):
        def rows(select_at):
            return [dict(kind='click',at=100,mode='build',token=7,queued=True,source_field=99,x=100,y=80),
                dict(kind='pad',at=101,buttons=0x4000,ui_mode='build',ui_token=7,ui_field=100,x=0,y=0),
                dict(kind='pad',at=105,buttons=0,ui_mode='game',x=0,y=0),
                dict(kind='pad',at=select_at,buttons=0x8000,ui_mode='game',ui_token=3,ui_field=select_at-1,
                    cursor_field=select_at-1,cursor_x=100,cursor_y=80,x=0,y=0),
                dict(kind='pad',at=select_at+4,buttons=0,x=0,y=0)]
        # ADR-067 menu pulses: Back held 4 fields, A after 4 neutral fields.
        self.assertTrue(hud.check_replay(rows(109),[])[0]['accepted'])
        self.assertTrue(hud.check_replay(rows(119),[])[0]['accepted'])
        self.assertFalse(hud.check_replay(rows(107),[])[0]['accepted'])

    def test_three_queued_hints_need_three_distinct_submitted_edges(self):
        hint=dict(group=4,buttons=2,x0=195,y0=178,x1=240,y1=195)
        rows=[]
        for field in (100,107,114):
            rows.append(dict(kind='click',at=field,mode='game',token=3,queued=True,
                source_field=field-1,x=220,y=185))
        for field in (101,119,137):
            rows.extend([dict(kind='pad',at=field,ui_mode='game',ui_token=3,ui_field=field-1,
                buttons=2,x=0,y=0,ui_items=[hint]),dict(kind='pad',at=field+6,buttons=0,x=0,y=0)])
        rows.sort(key=lambda r:r['at'])
        self.assertTrue(hud.check(rows,[])['hint_controller_accepted'])
        self.assertFalse(hud.check([r for r in rows if r['at']!=119],[])['hint_controller_accepted'])

    def test_hover_during_hud_queue_resumes_after_the_final_neutral_gap(self):
        rows=[dict(kind='hover',at=120,x=130,y=80,hud=False),
            dict(kind='pad',at=156,buttons=0,x=.3,y=0,cursor_field=155,cursor_x=100,cursor_y=80),
            dict(kind='pad',at=166,buttons=0,x=0,y=0,cursor_field=165,cursor_x=130,cursor_y=80)]
        results=[dict(click=dict(at=f),accepted=True,press=dict(at=p))
            for f,p in [(100,101),(107,119),(114,137)]]
        self.assertTrue(hud.check_queue(rows,results)[0]['hover_resumes'][0]['accepted'])
        rows[1]['at']=150
        self.assertFalse(hud.check_queue(rows,results)[0]['hover_resumes'][0]['accepted'])
        rows[1]['at']=156; rows[-1]['cursor_field']=130
        self.assertFalse(hud.check_queue(rows,results)[0]['hover_resumes'][0]['accepted'])

    def test_raw_controller_recipe_cannot_certify_a_hint_click(self):
        rows=[dict(kind='click',at=100,mode='game',token=3,queued=True,source_field=99,x=220,y=185),
            dict(kind='pad',at=101,ui_mode='game',ui_token=3,ui_field=100,buttons=2,x=0,y=0,
                ui_items=[dict(group=4,buttons=2,x0=195,y0=178,x1=240,y1=195)]),
            dict(kind='pad',at=106,buttons=0,x=0,y=0)]
        self.assertFalse(hud.check(rows,[dict(field=100,duration=12,buttons=2)])['hint_controller_accepted'])
        self.assertFalse(hud.check(rows,[])['physical_input_accepted'])

    def replay_rows(self):
        return [dict(kind='click',at=100,mode='build',token=300,queued=True,source_field=99,x=100,y=64),
            dict(kind='pad',at=101,ui_mode='build',ui_token=300,ui_field=100,buttons=0x4000,x=0,y=0),
            dict(kind='pad',at=103,ui_mode='game',buttons=0,x=0,y=0),
            dict(kind='pad',at=150,ui_mode='game',ui_token=3,ui_field=149,cursor_field=149,
                cursor_x=100.5,cursor_y=64.5,buttons=0x8000,x=0,y=0),
            dict(kind='pad',at=158,ui_mode='game',buttons=0,x=0,y=0)]

    def test_build_replay_requires_back_neutral_fresh_cursor_arrival_and_release(self):
        rows=self.replay_rows()
        self.assertTrue(hud.check(rows,[])['build_replay_accepted'])
        self.assertFalse(hud.check(rows[:4],[])['build_replay_accepted'])
        rows[3]['cursor_x']=150
        self.assertFalse(hud.check(rows,[])['build_replay_accepted'])
        rows[3]['cursor_x']=100; rows[3]['cursor_field']=100
        self.assertFalse(hud.check(rows,[])['build_replay_accepted'])
        rows[3]['cursor_field']=149; rows[1]['ui_token']=301
        self.assertFalse(hud.check(rows,[])['build_replay_accepted'])

    def test_raw_input_or_premature_select_cannot_certify_build_replay(self):
        rows=self.replay_rows()
        self.assertFalse(hud.check(rows,[dict(field=130,buttons=0x8000)])['build_replay_accepted'])
        self.assertFalse(hud.check(rows,[dict(field=130,point=[100,64])])['build_replay_accepted'])
        # Select inside the ADR-067 menu pulse spacing (Back at 101, spacing 8).
        rows[3]['at']=108; rows[3]['ui_field']=107; rows[3]['cursor_field']=107
        self.assertFalse(hud.check(rows,[])['build_replay_accepted'])

    def hover_rows(self):
        return [dict(kind='hover',at=100,mode='game',source_field=99,x=20,y=20,hud=False),
            dict(kind='pad',at=101,ui_mode='game',cursor_field=100,cursor_x=10,cursor_y=20,buttons=0,x=.3,y=0),
            dict(kind='pad',at=105,ui_mode='game',cursor_field=104,cursor_x=18,cursor_y=20,buttons=0,x=.1,y=0),
            dict(kind='pad',at=110,ui_mode='game',cursor_field=109,cursor_x=20,cursor_y=20,buttons=0,x=0,y=0),
            dict(kind='hover',at=120,mode='game',source_field=119,x=220,y=185,hud=True),
            dict(kind='pad',at=121,ui_mode='game',cursor_field=120,cursor_x=20,cursor_y=20,buttons=0,x=0,y=0),
            dict(kind='hover',at=125,mode='game',source_field=124,x=270,y=185,hud=True),
            dict(kind='info',at=140,ui_mode='game',cursor_field=139,cursor_x=20,cursor_y=20)]

    def test_hover_gate_requires_arrival_bounded_motion_and_hud_interception(self):
        rows=self.hover_rows()
        self.assertTrue(hover.check(rows)['hover_controller_accepted'])
        rows[2]['cursor_x']=80 # 17.5 px/field exceeds the ADR-094 cap plus margin
        self.assertFalse(hover.check(rows)['hover_controller_accepted'])
        rows[2]['cursor_x']=18; rows[5]['x']=.3
        self.assertFalse(hover.check(rows)['hover_controller_accepted'])

    @staticmethod
    def hover_pad(at,x,stick):
        return dict(kind='pad',at=at,ui_mode='game',cursor_field=at-1,cursor_x=x,cursor_y=20,buttons=0,x=stick,y=0)

    def approach_rows(self,*pads):
        return [dict(kind='hover',at=100,mode='game',source_field=99,x=20,y=20,hud=False),
                *[self.hover_pad(*pad) for pad in pads]]

    def test_hover_gate_accepts_adr094_speed_and_a_short_coasting_plateau(self):
        # 16 px/field would fail the former 8 px/field gate. The stick is released
        # at 16 px for two fields (the host logs the zero row once) while coasting.
        rows=self.approach_rows((101,-28,.5),(103,4,0),(105,12,.2),(107,19.5,0))
        result=hover.check(rows)
        self.assertTrue(result['hover_controller_accepted'], result['observations'][0])
        self.assertEqual(result['schema'],4)
        self.assertEqual(result['observations'][0]['mode'],'stick')

    def test_hover_gate_rejects_a_long_zero_stick_plateau_the_host_logs_once(self):
        # The host logs a pad row only when the stick changes, so a stall far from
        # the target is ONE zero row followed by info snapshots, then steering again.
        stalled=self.approach_rows((101,-20,.5),(103,-4,0),(165,12,.2),(167,19.5,0))
        stalled+=[dict(kind='info',at=at,ui_mode='game',cursor_field=at-1,cursor_x=-4,cursor_y=20) for at in range(110,160,10)]
        self.assertFalse(hover.check(stalled)['hover_controller_accepted'])
        limit=hover.MAX_FAR_ZERO_FIELDS
        edge=lambda fields: self.approach_rows((101,-20,.5),(103,-4,0),(103+fields,12,.2),(105+fields,19.5,0))
        self.assertTrue(hover.check(edge(limit))['hover_controller_accepted'])
        self.assertFalse(hover.check(edge(limit+1))['hover_controller_accepted'])

    def test_hover_gate_bounds_overshoot_but_rejects_moving_away(self):
        # A 3 px overshoot is within one step of coasting (observed up to 2.4 px).
        near=self.approach_rows((101,10,.5),(103,23,.1),(105,19.5,0))
        self.assertTrue(hover.check(near)['hover_controller_accepted'])
        far=self.approach_rows((101,10,.5),(103,19,0.2),(105,-5,.5),(107,19.5,0))  # 24 px back exceeds one 16 px step
        self.assertFalse(hover.check(far)['hover_controller_accepted'])

    def test_hover_gate_rejects_stale_sparse_and_stationary_without_arrival(self):
        rows=self.hover_rows()
        rows[0]['source_field']=80
        self.assertFalse(hover.check(rows)['hover_controller_accepted'])
        rows[0]['source_field']=99; rows[3]['cursor_x']=18
        self.assertFalse(hover.check(rows)['hover_controller_accepted'])
        self.assertFalse(hover.check(rows[:2])['hover_controller_accepted'])

    @staticmethod
    def direct_pad(at,x,y=20,buttons=0,stick=(0,0),direct=True):
        return dict(kind='pad',at=at,ui_mode='game',cursor_field=at-2,cursor_x=x,cursor_y=y,buttons=buttons,
                    x=stick[0],y=stick[1],source=dict(gesture=True,direct=direct))

    def direct_rows(self,target=(200,120),*pads):
        return [dict(kind='hover',at=100,mode='game',source_field=99,x=target[0],y=target[1],hud=False),*pads]

    def test_hover_gate_accepts_a_direct_cursor_jump_after_a_stick_field(self):
        rows=self.direct_rows((200,120),self.direct_pad(101,20,120,stick=(.5,0),direct=False),
            self.direct_pad(102,30,120),dict(kind='info',at=104,ui_mode='game',cursor_field=103,cursor_x=200,cursor_y=120),
            dict(kind='info',at=110,ui_mode='game',cursor_field=109,cursor_x=200,cursor_y=120))
        result=hover.check(rows)
        self.assertTrue(result['hover_controller_accepted'], result['observations'][0])
        self.assertEqual(result['observations'][0]['mode'],'direct')
        late=[dict(r) for r in rows]; late[3]['at']=130; late[3]['cursor_field']=129; del late[4]
        self.assertFalse(hover.check(late)['hover_controller_accepted'])  # arrives after the request age bound
        drift=rows+[dict(kind='info',at=116,ui_mode='game',cursor_field=115,cursor_x=190,cursor_y=120)]
        self.assertFalse(hover.check(drift)['hover_controller_accepted'])  # cursor leaves the target
        steer=rows+[self.direct_pad(112,200,120,stick=(.2,0))]
        self.assertFalse(hover.check(steer)['hover_controller_accepted'])  # no stick under the direct cursor
        once=rows[:4]
        self.assertFalse(hover.check(once)['hover_controller_accepted'])  # no later snapshot shows it stayed

    def test_hover_gate_accepts_z_edge_scroll_only_toward_the_targets_edge(self):
        top=lambda stick: self.direct_rows((160,0),self.direct_pad(101,160,0,0x2000,stick),
            dict(kind='info',at=104,ui_mode='game',cursor_field=103,cursor_x=160,cursor_y=0))
        self.assertTrue(hover.check(top((0,.5)))['hover_controller_accepted'])
        self.assertFalse(hover.check(top((0,-.5)))['hover_controller_accepted'])
        self.assertEqual(hover.edge_direction(319,239),(1,1))

    def test_unchanged_zero_stick_is_carried_across_hud_hover(self):
        rows=self.hover_rows()
        del rows[5] # zero stick remained unchanged, so the host emits no new pad row
        self.assertTrue(hover.check(rows)['hover_controller_accepted'])
        rows[3]['x']=.1
        self.assertFalse(hover.check(rows)['hover_controller_accepted'])


if __name__ == '__main__':
    unittest.main()
