#!/usr/bin/env python3
"""
ONVIF Camera & Pipeline Management UI - Integrated Version

Integrates with real ONVIF suite:
- discovery: discover_onvif_cameras()
- camera_profiles: read_camera_profiles()
- event_manager: get_supported_event_topics()

Usage:
    python3 demo_ui_integrated.py
"""

import tkinter as tk
from tkinter import ttk, messagebox, simpledialog
from datetime import datetime
from threading import Thread
import sys
from pathlib import Path

# Add parent path for imports
sys.path.insert(0, str(Path(__file__).parent.parent.parent.parent))

from dlstreamer.onvif.discovery import discover_onvif_cameras
from dlstreamer.onvif.camera_profiles import read_camera_profiles
from dlstreamer.onvif.event_manager import get_supported_event_topics, is_event_capable
from dlstreamer.onvif.ptz import PTZController, PTZVector, find_ptz_capable_profiles

try:
    import cv2
    OPENCV_AVAILABLE = True
except ImportError:
    OPENCV_AVAILABLE = False

try:
    from PIL import Image, ImageTk
    PIL_AVAILABLE = True
except ImportError:
    PIL_AVAILABLE = False


class CameraManagerUI:
    """Integrated UI for ONVIF camera discovery and management."""

    def __init__(self, root):
        self.root = root
        self.root.title("ONVIF Camera & Pipeline Manager - Integrated")
        self.root.geometry("1400x800")
        self.root.configure(bg="#f0f0f0")

        self.cameras = {}  # hostname:port -> camera_data
        self.profiles_cache = {}  # hostname:port -> profiles
        self.events_cache = {}  # hostname:port -> event topics
        self.selected_camera = None
        self.selected_profile = None
        self.discovery_thread = None
        self.credentials_username = ""
        self.credentials_password = ""
        
        # Video streaming
        self.video_thread = None
        self.stop_stream = False
        self.current_rtsp_url = None
        self.video_label = None
        
        # PTZ control
        self.ptz_controller = None
        self.ptz_speed = 0.5  # Default speed

        self._create_ui()

    def _create_ui(self):
        """Build the main UI with tabs and discovery controls."""
        style = ttk.Style()
        style.theme_use("clam")

        # Header
        header_frame = tk.Frame(self.root, bg="#2c3e50", height=50)
        header_frame.pack(fill=tk.X, padx=0, pady=0)
        header_label = tk.Label(
            header_frame,
            text="ONVIF Camera & Pipeline Manager - Integrated with Real API",
            bg="#2c3e50",
            fg="white",
            font=("Arial", 14, "bold"),
            pady=10,
        )
        header_label.pack()

        # Control panel
        control_frame = tk.Frame(self.root, bg="#ecf0f1", height=60)
        control_frame.pack(fill=tk.X, padx=5, pady=5)

        tk.Label(control_frame, text="Credentials:", bg="#ecf0f1").pack(
            side=tk.LEFT, padx=5
        )
        tk.Label(control_frame, text="Username:", bg="#ecf0f1").pack(side=tk.LEFT)
        self.username_entry = tk.Entry(control_frame, width=12)
        self.username_entry.insert(0, "admin")
        self.username_entry.pack(side=tk.LEFT, padx=3)

        tk.Label(control_frame, text="Password:", bg="#ecf0f1").pack(side=tk.LEFT)
        self.password_entry = tk.Entry(control_frame, width=12, show="*")
        self.password_entry.insert(0, "r00tme")
        self.password_entry.pack(side=tk.LEFT, padx=3)

        self.discover_btn = tk.Button(
            control_frame, text="🔍 Discover Cameras", command=self._start_discovery
        )
        self.discover_btn.pack(side=tk.LEFT, padx=5)

        tk.Button(
            control_frame, text="Add Manual Camera", command=self._add_manual_camera
        ).pack(side=tk.LEFT, padx=5)

        self.status_label = tk.Label(control_frame, text="Ready", bg="#ecf0f1", fg="#27ae60")
        self.status_label.pack(side=tk.LEFT, padx=20)

        # Notebook (tabs)
        notebook = ttk.Notebook(self.root)
        notebook.pack(fill=tk.BOTH, expand=True, padx=10, pady=10)

        # Tab 1: Cameras & Profiles
        self._create_cameras_tab(notebook)

        # Tab 2: Event Topics
        self._create_events_tab(notebook)

        # Tab 3: Pipelines (mock)
        self._create_pipelines_tab(notebook)

        # Footer
        footer_frame = tk.Frame(self.root, bg="#ecf0f1", height=30)
        footer_frame.pack(fill=tk.X, padx=0, pady=0)
        footer_label = tk.Label(
            footer_frame,
            text=f"Demo UI | Last updated: {datetime.now().strftime('%Y-%m-%d %H:%M:%S')}",
            bg="#ecf0f1",
            fg="#7f8c8d",
            font=("Arial", 9),
            pady=5,
        )
        footer_label.pack()

    def _create_cameras_tab(self, notebook):
        """Tab 1: Discovered cameras and profiles."""
        frame = ttk.Frame(notebook)
        notebook.add(frame, text="Cameras & Profiles")

        # Left panel: Camera list
        left_frame = ttk.Frame(frame)
        left_frame.pack(side=tk.LEFT, fill=tk.BOTH, expand=False, padx=5, pady=5)

        tk.Label(left_frame, text="Discovered Cameras", font=("Arial", 10, "bold")).pack(
            anchor=tk.W
        )
        
        # Create scrollable frame for camera list
        canvas = tk.Canvas(left_frame, bg="white", highlightthickness=0)
        scrollbar = tk.Scrollbar(left_frame, orient="vertical", command=canvas.yview)
        self.camera_list_frame = tk.Frame(canvas, bg="white")
        
        self.camera_list_frame.bind(
            "<Configure>",
            lambda e: canvas.configure(scrollregion=canvas.bbox("all"))
        )
        
        canvas.create_window((0, 0), window=self.camera_list_frame, anchor="nw")
        canvas.configure(yscrollcommand=scrollbar.set)
        
        canvas.pack(side=tk.LEFT, fill=tk.BOTH, expand=True)
        scrollbar.pack(side=tk.RIGHT, fill=tk.Y)
        
        self.camera_buttons = {}  # camera_id -> button widget

        # Right panel: Details, profiles and video
        right_frame = ttk.Frame(frame)
        right_frame.pack(side=tk.RIGHT, fill=tk.BOTH, expand=True, padx=5, pady=5)

        # Top: Camera details
        tk.Label(right_frame, text="Camera Details", font=("Arial", 10, "bold")).pack(
            anchor=tk.W
        )
        self.camera_details_text = tk.Text(right_frame, height=7, wrap=tk.WORD)
        self.camera_details_text.pack(fill=tk.X, pady=5)
        self.camera_details_text.config(state=tk.DISABLED)
        
        # Configure color tags for status
        self.camera_details_text.tag_config("status_online", foreground="green")
        self.camera_details_text.tag_config("status_offline", foreground="red")
        self.camera_details_text.tag_config("status_checking", foreground="orange")

        # Middle: Profiles
        tk.Label(right_frame, text="Media Profiles", font=("Arial", 10, "bold")).pack(
            anchor=tk.W, pady=(10, 0)
        )
        self.profile_tree = ttk.Treeview(
            right_frame, height=8, columns=("resolution", "encoding", "rtsp")
        )
        self.profile_tree.heading("#0", text="Profile Name")
        self.profile_tree.heading("resolution", text="Resolution")
        self.profile_tree.heading("encoding", text="Encoding")
        self.profile_tree.heading("rtsp", text="RTSP URL")
        self.profile_tree.column("#0", width=120)
        self.profile_tree.column("resolution", width=90)
        self.profile_tree.column("encoding", width=80)
        self.profile_tree.column("rtsp", width=150)
        self.profile_tree.pack(fill=tk.X, expand=False)
        self.profile_tree.bind("<<TreeviewSelect>>", self._on_profile_select)

        # Bottom: Video stream area
        video_container = tk.Frame(right_frame)
        video_container.pack(fill=tk.BOTH, expand=True, pady=(10, 0))

        # PTZ Control Panel (left side) - with minimum width
        ptz_frame = tk.LabelFrame(video_container, text="🎮 PTZ Control", bg="#f0f0f0", font=("Arial", 10, "bold"), width=180)
        ptz_frame.pack(side=tk.LEFT, fill=tk.Y, padx=5, pady=5)
        ptz_frame.pack_propagate(True)  # Allow frame to adjust height

        # Arrow buttons for Pan/Tilt
        arrow_frame = tk.Frame(ptz_frame, bg="#f0f0f0")
        arrow_frame.pack(pady=5)

        tk.Button(arrow_frame, text="⬆", width=3, height=1, command=self._ptz_up, bg="#e8f0fe", font=("Arial", 14), relief=tk.RAISED).grid(row=0, column=1, padx=2, pady=2)
        tk.Button(arrow_frame, text="⬅", width=3, height=1, command=self._ptz_left, bg="#e8f0fe", font=("Arial", 14), relief=tk.RAISED).grid(row=1, column=0, padx=2, pady=2)
        tk.Button(arrow_frame, text="⏹", width=3, height=1, command=self._ptz_stop, bg="#ffcccc", font=("Arial", 14), relief=tk.RAISED).grid(row=1, column=1, padx=2, pady=2)
        tk.Button(arrow_frame, text="➡", width=3, height=1, command=self._ptz_right, bg="#e8f0fe", font=("Arial", 14), relief=tk.RAISED).grid(row=1, column=2, padx=2, pady=2)
        tk.Button(arrow_frame, text="⬇", width=3, height=1, command=self._ptz_down, bg="#e8f0fe", font=("Arial", 14), relief=tk.RAISED).grid(row=2, column=1, padx=2, pady=2)

        # Zoom controls (horizontal)
        zoom_frame = tk.Frame(ptz_frame, bg="#f0f0f0")
        zoom_frame.pack(pady=5, fill=tk.X, padx=5)
        tk.Label(zoom_frame, text="Zoom:", font=("Arial", 8, "bold"), bg="#f0f0f0").pack(side=tk.LEFT)
        tk.Button(zoom_frame, text="🔍+", width=6, command=self._ptz_zoom_in, bg="#ceead6", font=("Arial", 10), relief=tk.RAISED).pack(side=tk.LEFT, padx=2)
        tk.Button(zoom_frame, text="🔍-", width=6, command=self._ptz_zoom_out, bg="#ceead6", font=("Arial", 10), relief=tk.RAISED).pack(side=tk.LEFT, padx=2)

        # Speed control (horizontal)
        speed_frame = tk.Frame(ptz_frame, bg="#f0f0f0")
        speed_frame.pack(pady=5, fill=tk.X, padx=5)
        tk.Label(speed_frame, text="Speed:", font=("Arial", 8, "bold"), bg="#f0f0f0").pack(side=tk.LEFT)
        self.speed_var = tk.StringVar(value="0.5")
        speed_combo = ttk.Combobox(
            speed_frame, textvariable=self.speed_var, values=["0.2", "0.5", "1.0"],
            width=5, state="readonly", font=("Arial", 9)
        )
        speed_combo.pack(side=tk.LEFT, padx=5)
        speed_combo.bind("<<ComboboxSelected>>", self._on_speed_change)

        # Live Stream Panel (right side)
        live_frame = tk.LabelFrame(video_container, text="📺 Live Stream", bg="#f0f0f0", font=("Arial", 10, "bold"))
        live_frame.pack(side=tk.RIGHT, fill=tk.BOTH, expand=True, padx=5, pady=5)
        self.live_frame = live_frame
        # Track available display area (stable container size, not the label itself)
        self.video_area_w = 480
        self.video_area_h = 320
        live_frame.bind("<Configure>", self._on_video_area_resize)

        if OPENCV_AVAILABLE and PIL_AVAILABLE:
            # Control buttons - pack at BOTTOM first so they always stay visible
            control_frame = tk.Frame(live_frame, bg="#f0f0f0")
            control_frame.pack(side=tk.BOTTOM, fill=tk.X, padx=5, pady=5)

            self.play_btn = tk.Button(
                control_frame, text="▶ Play Stream", command=self._start_stream, bg="#27ae60", fg="white", relief=tk.RAISED
            )
            self.play_btn.pack(side=tk.LEFT, padx=5)

            self.stop_btn = tk.Button(
                control_frame, text="⏹ Stop Stream", command=self._stop_stream, bg="#e74c3c", fg="white", state=tk.DISABLED, relief=tk.RAISED
            )
            self.stop_btn.pack(side=tk.LEFT, padx=5)

            tk.Label(control_frame, text="", bg="#f0f0f0").pack(side=tk.LEFT, expand=True)

            self.stream_info_label = tk.Label(control_frame, text="", bg="#f0f0f0", fg="#7f8c8d", font=("Arial", 8))
            self.stream_info_label.pack(side=tk.RIGHT, padx=5)

            # Video label - pack last, fills remaining space above controls
            self.video_label = tk.Label(
                live_frame, bg="#1a1a1a", fg="white", text="Select a profile to start streaming...", font=("Arial", 10), anchor=tk.CENTER
            )
            self.video_label.pack(side=tk.TOP, fill=tk.BOTH, expand=True)
        else:
            missing = []
            if not OPENCV_AVAILABLE:
                missing.append("opencv-python")
            if not PIL_AVAILABLE:
                missing.append("pillow")
            
            msg = f"Video streaming requires: {', '.join(missing)}\n\nInstall with:\npip install {' '.join(missing)}"
            tk.Label(
                live_frame,
                text=msg,
                bg="#f0f0f0",
                fg="#e74c3c",
                font=("Arial", 9),
                justify=tk.LEFT,
            ).pack(fill=tk.BOTH, expand=True, padx=10, pady=10)

    def _create_events_tab(self, notebook):
        """Tab 2: Event topics from camera GetEventProperties."""
        frame = ttk.Frame(notebook)
        notebook.add(frame, text="Event Topics")

        # Header with title and info
        header_frame = tk.Frame(frame, bg="#f0f0f0")
        header_frame.pack(fill=tk.X, padx=10, pady=10)

        tk.Label(header_frame, text="Event Topics & Properties", font=("Arial", 12, "bold"), bg="#f0f0f0").pack(anchor=tk.W)
        
        info_frame = tk.Frame(header_frame, bg="#f0f0f0")
        info_frame.pack(anchor=tk.W, pady=(5, 0))
        tk.Label(info_frame, text="Select a camera to view its supported events", font=("Arial", 9), fg="#666", bg="#f0f0f0").pack(anchor=tk.W)
        self.events_info_label = tk.Label(info_frame, text="", font=("Arial", 8), fg="#999", bg="#f0f0f0")
        self.events_info_label.pack(anchor=tk.W)

        # Button frame
        button_frame = tk.Frame(frame, bg="#f0f0f0")
        button_frame.pack(fill=tk.X, padx=10, pady=5)
        
        self.events_load_btn = tk.Button(
            button_frame, text="🔄 Reload Events", command=self._load_events_manually,
            bg="#3498db", fg="white", relief=tk.RAISED
        )
        self.events_load_btn.pack(side=tk.LEFT, padx=5)

        # Tree with scrollbar
        tree_frame = tk.Frame(frame)
        tree_frame.pack(fill=tk.BOTH, expand=True, padx=10, pady=5)

        scrollbar = ttk.Scrollbar(tree_frame)
        scrollbar.pack(side=tk.RIGHT, fill=tk.Y)

        self.events_tree = ttk.Treeview(
            tree_frame,
            columns=("kind", "type"),
            height=25,
            yscrollcommand=scrollbar.set
        )
        scrollbar.config(command=self.events_tree.yview)

        self.events_tree.heading("#0", text="Event / Property")
        self.events_tree.heading("kind", text="Details")
        self.events_tree.heading("type", text="Type")

        self.events_tree.column("#0", width=350)
        self.events_tree.column("kind", width=500)
        self.events_tree.column("type", width=100)

        self.events_tree.pack(fill=tk.BOTH, expand=True)

    def _create_pipelines_tab(self, notebook):
        """Tab 3: Pipelines (mock data for now)."""
        frame = ttk.Frame(notebook)
        notebook.add(frame, text="Pipelines & Bindings")

        # Pipelines
        tk.Label(frame, text="Analytics Pipelines", font=("Arial", 10, "bold")).pack(
            anchor=tk.W, padx=10, pady=5
        )

        self.pipelines_tree = ttk.Treeview(frame, columns=("type", "status"), height=10)
        self.pipelines_tree.heading("#0", text="Pipeline")
        self.pipelines_tree.heading("type", text="Type")
        self.pipelines_tree.heading("status", text="Status")
        self.pipelines_tree.column("#0", width=250)
        self.pipelines_tree.column("type", width=150)
        self.pipelines_tree.column("status", width=100)

        # Mock pipelines
        mock_pipelines = [
            ("motion_detection_pipeline", "analytic", "Loaded"),
            ("person_detection_pipeline", "analytic", "Loaded"),
            ("vehicle_tracking_pipeline", "analytic", "Not loaded"),
        ]
        for name, ptype, status in mock_pipelines:
            icon = "✓" if status == "Loaded" else "✗"
            self.pipelines_tree.insert(
                "", "end", name, text=name, values=(ptype, f"{icon} {status}")
            )

        self.pipelines_tree.pack(fill=tk.BOTH, expand=True, padx=10, pady=5)

        # Camera-Pipeline Bindings
        tk.Label(frame, text="Camera-Pipeline Bindings", font=("Arial", 10, "bold")).pack(
            anchor=tk.W, padx=10, pady=(20, 5)
        )

        self.bindings_tree = ttk.Treeview(
            frame, columns=("camera", "pipeline", "profile", "active"), height=8
        )
        self.bindings_tree.heading("#0", text="ID")
        self.bindings_tree.heading("camera", text="Camera")
        self.bindings_tree.heading("pipeline", text="Pipeline")
        self.bindings_tree.heading("profile", text="Profile")
        self.bindings_tree.heading("active", text="Active")

        self.bindings_tree.column("#0", width=80)
        self.bindings_tree.column("camera", width=250)
        self.bindings_tree.column("pipeline", width=200)
        self.bindings_tree.column("profile", width=150)
        self.bindings_tree.column("active", width=80)

        self.bindings_tree.pack(fill=tk.BOTH, expand=True, padx=10, pady=5)

    def _start_discovery(self):
        """Start camera discovery in background thread."""
        self.discover_btn.config(state=tk.DISABLED)
        self.status_label.config(text="Discovering cameras...", fg="#e74c3c")
        
        self.credentials_username = self.username_entry.get()
        self.credentials_password = self.password_entry.get()

        self.discovery_thread = Thread(target=self._discovery_worker, daemon=True)
        self.discovery_thread.start()

    def _discovery_worker(self):
        """Background worker for camera discovery."""
        try:
            discovered = []
            for cam_desc in discover_onvif_cameras(verbose=False):
                hostname = cam_desc.get("hostname")
                port = cam_desc.get("port")
                cam_id = f"{hostname}:{port}"

                self.cameras[cam_id] = cam_desc
                discovered.append(cam_id)

                # Update UI from main thread
                self.root.after(0, self._add_camera_to_tree, cam_id)

            if discovered:
                msg = f"Discovered {len(discovered)} camera(s)"
                self.root.after(0, lambda: self.status_label.config(
                    text=msg, fg="#27ae60"))
            else:
                self.root.after(0, lambda: self.status_label.config(
                    text="No cameras found", fg="#e74c3c"))

        except Exception as e:
            msg = f"Discovery error: {str(e)}"
            self.root.after(0, lambda: messagebox.showerror("Discovery Error", msg))
            self.root.after(0, lambda: self.status_label.config(
                text="Discovery failed", fg="#e74c3c"))
        finally:
            self.root.after(0, lambda: self.discover_btn.config(state=tk.NORMAL))

    def _add_camera_to_tree(self, cam_id):
        """Add discovered camera to list with status."""
        # Create button for this camera
        btn = tk.Button(
            self.camera_list_frame,
            text=f"🔍 {cam_id}",
            bg="#f39c12",
            fg="#000000",
            font=("Arial", 10),
            anchor="w",
            padx=10,
            pady=8,
            relief=tk.FLAT,
            activebackground="#e67e22",
            activeforeground="#000000",
            command=lambda: self._on_camera_select_by_id(cam_id)
        )
        btn.pack(fill=tk.X, padx=2, pady=2)
        self.camera_buttons[cam_id] = btn
        
        # Check if camera is online in background
        def check_online():
            try:
                cam_data = self.cameras.get(cam_id)
                if cam_data:
                    hostname = cam_data.get("hostname")
                    port = cam_data.get("port")
                    # Quick probe - try to get event capabilities
                    is_online = is_event_capable(
                        hostname, 
                        port, 
                        username=self.credentials_username,
                        password=self.credentials_password,
                        verbose=False
                    )
                    if is_online:
                        self.root.after(0, lambda: self._update_camera_button_status(cam_id, True))
                    else:
                        self.root.after(0, lambda: self._update_camera_button_status(cam_id, False))
            except Exception:
                # If probe fails, mark as offline
                self.root.after(0, lambda: self._update_camera_button_status(cam_id, False))
        
        Thread(target=check_online, daemon=True).start()

    def _update_camera_button_status(self, cam_id, is_online):
        """Update camera button color and text based on online status."""
        if cam_id not in self.camera_buttons:
            return
        
        btn = self.camera_buttons[cam_id]
        if is_online:
            btn.config(
                text=f"🟢 {cam_id}",
                bg="#27ae60",
                activebackground="#229954",
                fg="white",
                activeforeground="white"
            )
        else:
            btn.config(
                text=f"🔴 {cam_id}",
                bg="#e74c3c",
                activebackground="#c0392b",
                fg="white",
                activeforeground="white"
            )

    def _on_camera_select_by_id(self, cam_id):
        """Handle camera selection by ID."""
        self.selected_camera = cam_id
        cam_data = self.cameras.get(cam_id)
        if not cam_data:
            return

        hostname = cam_data.get("hostname")
        port = cam_data.get("port")

        # Show initial "Checking" status
        self._update_camera_details_display(cam_id, "checking")

        # Load profiles and events in background
        self._load_profiles_for_camera(hostname, port)
        self._load_events_for_camera(hostname, port)

    def _update_camera_details_display(self, cam_id, status_override=None):
        """Update camera details display with current cached data."""
        cam_data = self.cameras.get(cam_id)
        if not cam_data:
            return

        hostname = cam_data.get("hostname")
        port = cam_data.get("port")

        # Determine status
        if status_override == "checking":
            status = "🟡 Checking..."
        elif status_override == "online":
            status = "🟢 Online"
        elif status_override == "offline":
            status = "🔴 Offline"
        else:
            status = "🟡 Unknown"
        
        profiles_count = len(self.profiles_cache.get(cam_id, []))
        events_count = len(self.events_cache.get(cam_id, []))
        
        # Check for PTZ capability
        has_ptz = False
        if cam_id in self.profiles_cache:
            for profile in self.profiles_cache[cam_id]:
                if profile.ptz_token:
                    has_ptz = True
                    break
        
        details = (
            f"📺 {hostname}:{port}\n"
            f"ID: {cam_id}\n"
            f"Status: {status}\n"
            f"Profiles: {profiles_count}\n"
            f"Events: {events_count} | PTZ: {'Yes ✓' if has_ptz else 'No'}\n"
            f"User: {self.credentials_username or '(default)'}"
        )
        
        # Determine tag based on status
        if status_override == "online":
            tag = "status_online"
        elif status_override == "offline":
            tag = "status_offline"
        else:
            tag = "status_checking"
        
        self.camera_details_text.config(state=tk.NORMAL)
        self.camera_details_text.delete(1.0, tk.END)
        self.camera_details_text.insert(1.0, details, tag)
        self.camera_details_text.config(state=tk.DISABLED)


    def _add_manual_camera(self):
        """Add camera manually."""
        dialog = tk.Toplevel(self.root)
        dialog.title("Add Camera")
        dialog.geometry("300x150")

        tk.Label(dialog, text="Hostname:").pack(pady=5)
        hostname_entry = tk.Entry(dialog)
        hostname_entry.pack(pady=5)

        tk.Label(dialog, text="Port:").pack(pady=5)
        port_entry = tk.Entry(dialog)
        port_entry.insert(0, "80")
        port_entry.pack(pady=5)

        def add():
            try:
                hostname = hostname_entry.get().strip()
                port = int(port_entry.get().strip())
                if not hostname:
                    messagebox.showerror("Error", "Hostname required")
                    return

                cam_id = f"{hostname}:{port}"
                self.cameras[cam_id] = {"hostname": hostname, "port": port}
                self._add_camera_to_tree(cam_id)
                dialog.destroy()
            except ValueError:
                messagebox.showerror("Error", "Invalid port number")

        tk.Button(dialog, text="Add", command=add).pack(pady=10)

    def _on_camera_select(self, event):
        """Handle camera selection (legacy - not used with new button approach)."""
        pass

    def _load_profiles_for_camera(self, hostname, port):
        """Load camera profiles in background thread."""
        cam_id = f"{hostname}:{port}"
        
        def worker():
            try:
                cam_iter = [{"hostname": hostname, "port": port}]
                for result in read_camera_profiles(
                    cam_iter,
                    username=self.credentials_username,
                    password=self.credentials_password,
                    verbose=False,
                ):
                    if result.ok:
                        profiles = result.profiles
                        self.profiles_cache[cam_id] = profiles

                        # Update UI - profiles and camera status
                        self.root.after(0, self._update_profiles_tree, profiles)
                        # Update camera details status to online if this is selected camera
                        if self.selected_camera == cam_id:
                            self.root.after(0, self._update_camera_details_display, cam_id, "online")
                    else:
                        error = result.error or "Unknown error"
                        # Update camera details status to offline if this is selected camera
                        if self.selected_camera == cam_id:
                            self.root.after(0, self._update_camera_details_display, cam_id, "offline")
                        self.root.after(
                            0,
                            lambda e=error: messagebox.showerror(
                                "Profile Error", f"Failed to read profiles: {e}"
                            ),
                        )
            except Exception as e:
                # Update camera details status to offline if this is selected camera
                if self.selected_camera == cam_id:
                    self.root.after(0, self._update_camera_details_display, cam_id, "offline")
                self.root.after(
                    0,
                    lambda e=e: messagebox.showerror(
                        "Profile Error", f"Exception: {str(e)}"
                    ),
                )

        Thread(target=worker, daemon=True).start()

    def _update_profiles_tree(self, profiles):
        """Update profiles tree with loaded data."""
        self.profile_tree.delete(*self.profile_tree.get_children())
        for profile in profiles:
            # ONVIFProfile is a dataclass with attributes, not a dict
            token = profile.token
            name = profile.name
            encoding = profile.vec_encoding or "N/A"
            
            # Extract resolution from vec_resolution dict
            resolution = "N/A"
            if profile.vec_resolution:
                width = profile.vec_resolution.get("width", "")
                height = profile.vec_resolution.get("height", "")
                if width and height:
                    resolution = f"{width}x{height}"
            
            rtsp_uri = profile.rtsp_url or ""

            self.profile_tree.insert(
                "",
                "end",
                token,
                text=name,
                values=(resolution, encoding, rtsp_uri),
            )

    def _load_events_for_camera(self, hostname, port):
        """Load supported event topics in background thread for selected camera."""
        cam_id = f"{hostname}:{port}"
        self.selected_camera = cam_id  # Store as string, not tuple
        
        # Show loading state
        self.events_info_label.config(text=f"Loading events for {hostname}:{port}...")
        self.events_load_btn.config(state=tk.DISABLED)
        
        def worker():
            try:
                topics = get_supported_event_topics(
                    hostname,
                    port,
                    username=self.credentials_username,
                    password=self.credentials_password,
                    verbose=False,
                )
                self.events_cache[cam_id] = topics
                
                # Update UI
                self.root.after(0, self._update_events_tree, topics, hostname, port)
            except Exception as e:
                # Camera may not support events
                error_msg = f"Failed to load events: {str(e)}"
                self.root.after(0, self._update_events_tree, [], hostname, port, error_msg)

        Thread(target=worker, daemon=True).start()

    def _load_events_manually(self):
        """Reload events for the currently selected camera."""
        if not hasattr(self, 'selected_camera') or not self.selected_camera:
            messagebox.showinfo("Info", "Select a camera first")
            return
        
        # Parse selected_camera string format "hostname:port"
        parts = self.selected_camera.split(':')
        if len(parts) != 2:
            messagebox.showerror("Error", "Invalid camera format")
            return
        hostname, port = parts[0], int(parts[1])
        self._load_events_for_camera(hostname, port)

    def _update_events_tree(self, topics, hostname=None, port=None, error_msg=None):
        """Update events tree - flat list with indentation for properties (like test app)."""
        self.events_tree.delete(*self.events_tree.get_children())
        
        # Update info label
        if error_msg:
            self.events_info_label.config(text=f"⚠ {error_msg}", fg="#e74c3c")
        else:
            event_count = len([t for t in topics if not t.is_property])
            prop_count = len([t for t in topics if t.is_property])
            info_text = f"Events: {event_count} | Properties: {prop_count}"
            if hostname and port:
                info_text += f" | Camera: {hostname}:{port}"
            self.events_info_label.config(text=info_text, fg="#27ae60")
        
        self.events_load_btn.config(state=tk.NORMAL)
        
        if not topics:
            if not error_msg:
                self.events_tree.insert("", "end", text="No events found", values=("—", "—"))
            return
        
        # Display all topics in flat list (matching event_manager_test_app behavior)
        for idx, topic in enumerate(topics, 1):
            topic_path = topic.topic
            is_property = topic.is_property
            source_info = self._format_dict_fields(topic.source)
            data_info = self._format_dict_fields(topic.data)
            
            # Format details: single line with | separator (no newlines for treeview)
            details_parts = []
            if source_info:
                details_parts.append(f"[Source: {source_info}]")
            if data_info:
                details_parts.append(f"[Data: {data_info}]")
            details = " | ".join(details_parts) if details_parts else "—"
            
            kind = "Property" if is_property else "Event"
            
            if is_property:
                display_text = f"    ◆ {topic_path}"  # Indent properties with spaces
            else:
                display_text = f"📡 {topic_path}"
            
            # Insert all to root ("") - flat structure
            self.events_tree.insert("", "end", text=display_text, values=(details, kind))






    def _format_dict_fields(self, data_dict):
        """Format dictionary fields for display."""
        if not data_dict:
            return "—"
        return ", ".join([f"{k}: {v}" for k, v in data_dict.items()])


    def _on_profile_select(self, event):
        """Handle profile selection."""
        selection = self.profile_tree.selection()
        if not selection:
            return

        profile_token = selection[0]
        self.selected_profile = profile_token
        
        # Initialize PTZ controller for this profile if available
        self._init_ptz_controller()

    def _init_ptz_controller(self):
        """Initialize PTZ controller for selected camera and profile."""
        if not self.selected_camera or not self.selected_profile:
            return
        
        try:
            # Close previous controller if exists
            if self.ptz_controller:
                self.ptz_controller.close()
                self.ptz_controller = None
            
            cam_data = self.cameras.get(self.selected_camera)
            if not cam_data:
                return
            
            hostname = cam_data.get("hostname")
            port = cam_data.get("port")
            
            # Find the profile object
            profiles = self.profiles_cache.get(self.selected_camera, [])
            selected_profile_obj = None
            for p in profiles:
                if p.token == self.selected_profile:
                    selected_profile_obj = p
                    break
            
            if not selected_profile_obj or not selected_profile_obj.ptz_token:
                # No PTZ support for this profile
                return
            
            # Create PTZ controller
            self.ptz_controller = PTZController(
                hostname=hostname,
                port=port,
                profile_token=self.selected_profile,
                username=self.credentials_username,
                password=self.credentials_password
            )
        except Exception as e:
            # Silent fail - not all profiles support PTZ
            self.ptz_controller = None

    def _ptz_up(self):
        """Pan-tilt up."""
        if not self.ptz_controller:
            return
        try:
            velocity = PTZVector(pan=0, tilt=self.ptz_speed)
            self.ptz_controller.continuous_move(velocity, timeout=0.5)
        except Exception as e:
            messagebox.showerror("PTZ Error", f"Failed to move: {str(e)}")

    def _ptz_down(self):
        """Pan-tilt down."""
        if not self.ptz_controller:
            return
        try:
            velocity = PTZVector(pan=0, tilt=-self.ptz_speed)
            self.ptz_controller.continuous_move(velocity, timeout=0.5)
        except Exception as e:
            messagebox.showerror("PTZ Error", f"Failed to move: {str(e)}")

    def _ptz_left(self):
        """Pan-tilt left."""
        if not self.ptz_controller:
            return
        try:
            velocity = PTZVector(pan=-self.ptz_speed, tilt=0)
            self.ptz_controller.continuous_move(velocity, timeout=0.5)
        except Exception as e:
            messagebox.showerror("PTZ Error", f"Failed to move: {str(e)}")

    def _ptz_right(self):
        """Pan-tilt right."""
        if not self.ptz_controller:
            return
        try:
            velocity = PTZVector(pan=self.ptz_speed, tilt=0)
            self.ptz_controller.continuous_move(velocity, timeout=0.5)
        except Exception as e:
            messagebox.showerror("PTZ Error", f"Failed to move: {str(e)}")

    def _ptz_zoom_in(self):
        """Zoom in."""
        if not self.ptz_controller:
            return
        try:
            velocity = PTZVector(zoom=self.ptz_speed)
            self.ptz_controller.continuous_move(velocity, timeout=0.5)
        except Exception as e:
            messagebox.showerror("PTZ Error", f"Failed to zoom: {str(e)}")

    def _ptz_zoom_out(self):
        """Zoom out."""
        if not self.ptz_controller:
            return
        try:
            velocity = PTZVector(zoom=-self.ptz_speed)
            self.ptz_controller.continuous_move(velocity, timeout=0.5)
        except Exception as e:
            messagebox.showerror("PTZ Error", f"Failed to zoom: {str(e)}")

    def _ptz_stop(self):
        """Stop all PTZ movement."""
        if not self.ptz_controller:
            return
        try:
            self.ptz_controller.stop()
        except Exception as e:
            messagebox.showerror("PTZ Error", f"Failed to stop: {str(e)}")

    def _on_speed_change(self, event=None):
        """Handle speed change."""
        try:
            self.ptz_speed = float(self.speed_var.get())
        except ValueError:
            self.ptz_speed = 0.5


    def _start_stream(self):
        """Start streaming from selected profile."""
        if not self.selected_profile or not self.video_label:
            messagebox.showwarning("Warning", "Please select a profile first")
            return

        # Get RTSP URL from selected profile
        profile_id = self.selected_profile
        cam_id = self.selected_camera
        
        if not cam_id or not self.profiles_cache.get(cam_id):
            messagebox.showerror("Error", "Camera or profiles not loaded")
            return

        profile = None
        for p in self.profiles_cache[cam_id]:
            if p.token == profile_id:
                profile = p
                break

        if not profile:
            messagebox.showerror("Error", "Profile not found")
            return

        rtsp_url = profile.rtsp_url
        if not rtsp_url:
            messagebox.showerror("Error", "RTSP URL not available for this profile")
            return

        # Add credentials to RTSP URL if needed
        if self.credentials_username or self.credentials_password:
            # Parse URL and insert credentials
            # Format: rtsp://[username[:password]@]hostname[:port][/path]
            if rtsp_url.startswith("rtsp://"):
                # Remove rtsp://
                remainder = rtsp_url[7:]
                
                # Check if credentials already in URL
                if "@" not in remainder:
                    # Add credentials
                    creds = f"{self.credentials_username}:{self.credentials_password}@" if self.credentials_username else ""
                    rtsp_url = f"rtsp://{creds}{remainder}"

        self.current_rtsp_url = rtsp_url
        self.stop_stream = False
        self.play_btn.config(state=tk.DISABLED)
        self.stop_btn.config(state=tk.NORMAL)
        self.stream_info_label.config(text=f"Connecting...")

        # Start video thread
        self.video_thread = Thread(target=self._video_worker, daemon=True)
        self.video_thread.start()

    def _stop_stream(self):
        """Stop streaming."""
        self.stop_stream = True
        self.play_btn.config(state=tk.NORMAL)
        self.stop_btn.config(state=tk.DISABLED)
        self.stream_info_label.config(text="Stream stopped")
        
        if self.video_label:
            self.video_label.config(image="", text="Stream stopped. Select a profile to start.")

    def _on_video_area_resize(self, event):
        """Track available video display area from the stable container size."""
        # Reserve space for the LabelFrame title and the control bar below the video
        self.video_area_w = max(event.width - 16, 100)
        self.video_area_h = max(event.height - 70, 100)

    def _video_worker(self):
        """Background worker to read RTSP stream and display frames."""
        if not OPENCV_AVAILABLE:
            return

        try:
            cap = cv2.VideoCapture(self.current_rtsp_url)
            if not cap.isOpened():
                self.root.after(0, lambda: messagebox.showerror("Error", "Cannot connect to RTSP stream"))
                self.root.after(0, self._stop_stream)
                return

            # Get stream properties
            fps = cap.get(cv2.CAP_PROP_FPS)
            width = int(cap.get(cv2.CAP_PROP_FRAME_WIDTH))
            height = int(cap.get(cv2.CAP_PROP_FRAME_HEIGHT))
            
            frame_count = 0

            while not self.stop_stream:
                ret, frame = cap.read()
                if not ret:
                    self.root.after(0, lambda: messagebox.showerror("Error", "Stream ended unexpectedly"))
                    self.root.after(0, self._stop_stream)
                    break

                frame_count += 1

                # Scale frame to fit the available container area, preserving aspect ratio
                area_w = max(getattr(self, "video_area_w", 480), 1)
                area_h = max(getattr(self, "video_area_h", 320), 1)
                aspect_ratio = width / height if height else 1.0
                display_width = area_w
                display_height = int(display_width / aspect_ratio)
                if display_height > area_h:
                    display_height = area_h
                    display_width = int(display_height * aspect_ratio)
                display_width = max(display_width, 1)
                display_height = max(display_height, 1)

                frame_resized = cv2.resize(frame, (display_width, display_height))
                frame_rgb = cv2.cvtColor(frame_resized, cv2.COLOR_BGR2RGB)
                
                img = Image.fromarray(frame_rgb)
                photo = ImageTk.PhotoImage(img)

                # Update label with frame
                self.root.after(0, lambda p=photo, fc=frame_count, f=fps: self._update_video_frame(p, fc, f))

                # Control frame rate
                delay = int(1000 / (fps if fps > 0 else 30))
                if delay > 0:
                    cv2.waitKey(delay)

        except Exception as e:
            self.root.after(0, lambda e=e: messagebox.showerror("Stream Error", f"Error: {str(e)}"))
            self.root.after(0, self._stop_stream)
        finally:
            if 'cap' in locals():
                cap.release()

    def _update_video_frame(self, photo, frame_count, fps):
        """Update video label with new frame."""
        if self.video_label and not self.stop_stream:
            self.video_label.config(image=photo, text="")
            self.video_label.image = photo  # Keep a reference
            
            fps_display = f"{fps:.1f} FPS" if fps > 0 else "N/A"
            self.stream_info_label.config(text=f"Frame: {frame_count} | {fps_display}")


def main():
    """Launch the integrated UI."""
    root = tk.Tk()
    app = CameraManagerUI(root)
    root.mainloop()


if __name__ == "__main__":
    main()
