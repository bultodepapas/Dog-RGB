// Generated from local Supabase migrations. Do not edit by hand.
// Regenerate with: npm run cloud:types:generate

export type Json =
  | string
  | number
  | boolean
  | null
  | { [key: string]: Json | undefined }
  | Json[]

export type Database = {
  api: {
    Tables: {
      collars: {
        Row: {
          capability_hash: string | null
          capability_manifest: Json | null
          config_schema: number | null
          created_at: string
          device_public_id: string
          display_name: string | null
          dog_id: string
          firmware_version: string | null
          hardware_revision: string | null
          id: string
          last_sync_at: string | null
          linked_at: string | null
          protocol_version: number | null
          revoked_at: string | null
          state: string
          telemetry_schema: number | null
          updated_at: string
        }
        Insert: {
          capability_hash?: string | null
          capability_manifest?: Json | null
          config_schema?: number | null
          created_at?: string
          device_public_id: string
          display_name?: string | null
          dog_id: string
          firmware_version?: string | null
          hardware_revision?: string | null
          id?: string
          last_sync_at?: string | null
          linked_at?: string | null
          protocol_version?: number | null
          revoked_at?: string | null
          state?: string
          telemetry_schema?: number | null
          updated_at?: string
        }
        Update: {
          capability_hash?: string | null
          capability_manifest?: Json | null
          config_schema?: number | null
          created_at?: string
          device_public_id?: string
          display_name?: string | null
          dog_id?: string
          firmware_version?: string | null
          hardware_revision?: string | null
          id?: string
          last_sync_at?: string | null
          linked_at?: string | null
          protocol_version?: number | null
          revoked_at?: string | null
          state?: string
          telemetry_schema?: number | null
          updated_at?: string
        }
        Relationships: [
          {
            foreignKeyName: "collars_dog_id_fkey"
            columns: ["dog_id"]
            isOneToOne: false
            referencedRelation: "dogs"
            referencedColumns: ["id"]
          },
        ]
      }
      config_reported: {
        Row: {
          cloud_received_at: string
          collar_id: string
          config_schema: number
          device_applied_at: string | null
          error_code: string | null
          firmware_version: string
          reported_body_sha256: string
          reported_server_version: number
          resource_key: string
          status: string
        }
        Insert: {
          cloud_received_at?: string
          collar_id: string
          config_schema: number
          device_applied_at?: string | null
          error_code?: string | null
          firmware_version: string
          reported_body_sha256: string
          reported_server_version: number
          resource_key: string
          status: string
        }
        Update: {
          cloud_received_at?: string
          collar_id?: string
          config_schema?: number
          device_applied_at?: string | null
          error_code?: string | null
          firmware_version?: string
          reported_body_sha256?: string
          reported_server_version?: number
          resource_key?: string
          status?: string
        }
        Relationships: [
          {
            foreignKeyName: "config_reported_collar_id_fkey"
            columns: ["collar_id"]
            isOneToOne: false
            referencedRelation: "collars"
            referencedColumns: ["id"]
          },
        ]
      }
      config_resource_heads: {
        Row: {
          accepted_actor_id: string
          accepted_hlc_logical: number
          accepted_hlc_physical_ms: number
          body: Json
          body_sha256: string
          collar_id: string
          resource_key: string
          resource_schema: number
          server_version: number
          updated_at: string
          winning_revision_id: string
        }
        Insert: {
          accepted_actor_id: string
          accepted_hlc_logical: number
          accepted_hlc_physical_ms: number
          body: Json
          body_sha256: string
          collar_id: string
          resource_key: string
          resource_schema: number
          server_version: number
          updated_at?: string
          winning_revision_id: string
        }
        Update: {
          accepted_actor_id?: string
          accepted_hlc_logical?: number
          accepted_hlc_physical_ms?: number
          body?: Json
          body_sha256?: string
          collar_id?: string
          resource_key?: string
          resource_schema?: number
          server_version?: number
          updated_at?: string
          winning_revision_id?: string
        }
        Relationships: [
          {
            foreignKeyName: "config_resource_heads_collar_id_fkey"
            columns: ["collar_id"]
            isOneToOne: false
            referencedRelation: "collars"
            referencedColumns: ["id"]
          },
          {
            foreignKeyName: "config_resource_heads_winning_revision_id_fkey"
            columns: ["winning_revision_id"]
            isOneToOne: false
            referencedRelation: "config_revisions"
            referencedColumns: ["id"]
          },
        ]
      }
      config_revisions: {
        Row: {
          accepted_actor_id: string
          accepted_hlc_logical: number
          accepted_hlc_physical_ms: number
          actor_device_id: string | null
          actor_user_id: string | null
          base_server_version: number | null
          body: Json
          body_sha256: string
          collar_id: string
          disposition: string
          id: string
          mutation_id: string
          ordering_mode: string
          origin: string
          received_at: string
          rejection_code: string | null
          resource_key: string
          resource_schema: number
          server_version: number | null
          submitted_actor_id: string
          submitted_hlc_logical: number
          submitted_hlc_physical_ms: number
          submitted_time_quality: string
        }
        Insert: {
          accepted_actor_id: string
          accepted_hlc_logical: number
          accepted_hlc_physical_ms: number
          actor_device_id?: string | null
          actor_user_id?: string | null
          base_server_version?: number | null
          body: Json
          body_sha256: string
          collar_id: string
          disposition: string
          id?: string
          mutation_id: string
          ordering_mode: string
          origin: string
          received_at?: string
          rejection_code?: string | null
          resource_key: string
          resource_schema: number
          server_version?: number | null
          submitted_actor_id: string
          submitted_hlc_logical: number
          submitted_hlc_physical_ms: number
          submitted_time_quality: string
        }
        Update: {
          accepted_actor_id?: string
          accepted_hlc_logical?: number
          accepted_hlc_physical_ms?: number
          actor_device_id?: string | null
          actor_user_id?: string | null
          base_server_version?: number | null
          body?: Json
          body_sha256?: string
          collar_id?: string
          disposition?: string
          id?: string
          mutation_id?: string
          ordering_mode?: string
          origin?: string
          received_at?: string
          rejection_code?: string | null
          resource_key?: string
          resource_schema?: number
          server_version?: number | null
          submitted_actor_id?: string
          submitted_hlc_logical?: number
          submitted_hlc_physical_ms?: number
          submitted_time_quality?: string
        }
        Relationships: [
          {
            foreignKeyName: "config_revisions_collar_id_fkey"
            columns: ["collar_id"]
            isOneToOne: false
            referencedRelation: "collars"
            referencedColumns: ["id"]
          },
        ]
      }
      daily_summaries: {
        Row: {
          algorithm_version: number
          average_moving_cmps: number | null
          average_observed_cmps: number | null
          computed_at: string
          coverage_ratio: number
          distance_m: number
          dog_id: string
          dropped_points: number
          filtered_max_speed_cmps: number | null
          gap_count: number
          inactive_s: number
          local_date: string
          moving_s: number
          observed_s: number
          source_revision: number
          timezone: string
          unknown_s: number
          valid_points: number
          warning_points: number
        }
        Insert: {
          algorithm_version: number
          average_moving_cmps?: number | null
          average_observed_cmps?: number | null
          computed_at?: string
          coverage_ratio: number
          distance_m: number
          dog_id: string
          dropped_points: number
          filtered_max_speed_cmps?: number | null
          gap_count: number
          inactive_s: number
          local_date: string
          moving_s: number
          observed_s: number
          source_revision: number
          timezone: string
          unknown_s: number
          valid_points: number
          warning_points: number
        }
        Update: {
          algorithm_version?: number
          average_moving_cmps?: number | null
          average_observed_cmps?: number | null
          computed_at?: string
          coverage_ratio?: number
          distance_m?: number
          dog_id?: string
          dropped_points?: number
          filtered_max_speed_cmps?: number | null
          gap_count?: number
          inactive_s?: number
          local_date?: string
          moving_s?: number
          observed_s?: number
          source_revision?: number
          timezone?: string
          unknown_s?: number
          valid_points?: number
          warning_points?: number
        }
        Relationships: [
          {
            foreignKeyName: "daily_summaries_dog_id_fkey"
            columns: ["dog_id"]
            isOneToOne: false
            referencedRelation: "dogs"
            referencedColumns: ["id"]
          },
        ]
      }
      dog_memberships: {
        Row: {
          created_at: string
          dog_id: string
          role: string
          user_id: string
        }
        Insert: {
          created_at?: string
          dog_id: string
          role: string
          user_id: string
        }
        Update: {
          created_at?: string
          dog_id?: string
          role?: string
          user_id?: string
        }
        Relationships: [
          {
            foreignKeyName: "dog_memberships_dog_id_fkey"
            columns: ["dog_id"]
            isOneToOne: false
            referencedRelation: "dogs"
            referencedColumns: ["id"]
          },
        ]
      }
      dogs: {
        Row: {
          birth_date: string | null
          breed: string | null
          created_at: string
          created_by: string
          deleted_at: string | null
          id: string
          name: string
          timezone: string
          timezone_effective_at: string
          updated_at: string
          weight_kg: number | null
        }
        Insert: {
          birth_date?: string | null
          breed?: string | null
          created_at?: string
          created_by: string
          deleted_at?: string | null
          id?: string
          name: string
          timezone?: string
          timezone_effective_at?: string
          updated_at?: string
          weight_kg?: number | null
        }
        Update: {
          birth_date?: string | null
          breed?: string | null
          created_at?: string
          created_by?: string
          deleted_at?: string | null
          id?: string
          name?: string
          timezone?: string
          timezone_effective_at?: string
          updated_at?: string
          weight_kg?: number | null
        }
        Relationships: []
      }
      profiles: {
        Row: {
          created_at: string
          default_timezone: string
          display_name: string | null
          units: string
          updated_at: string
          user_id: string
        }
        Insert: {
          created_at?: string
          default_timezone?: string
          display_name?: string | null
          units?: string
          updated_at?: string
          user_id: string
        }
        Update: {
          created_at?: string
          default_timezone?: string
          display_name?: string | null
          units?: string
          updated_at?: string
          user_id?: string
        }
        Relationships: []
      }
      recording_summaries: {
        Row: {
          algorithm_version: number
          average_moving_cmps: number | null
          average_observed_cmps: number | null
          computed_at: string
          coverage_ratio: number
          distance_m: number
          dropped_points: number
          filtered_max_speed_cmps: number | null
          gap_count: number
          inactive_s: number
          moving_s: number
          observed_s: number
          phase_durations: Json | null
          recording_id: string
          unknown_s: number
          valid_points: number
          warning_points: number
        }
        Insert: {
          algorithm_version: number
          average_moving_cmps?: number | null
          average_observed_cmps?: number | null
          computed_at?: string
          coverage_ratio: number
          distance_m: number
          dropped_points: number
          filtered_max_speed_cmps?: number | null
          gap_count: number
          inactive_s: number
          moving_s: number
          observed_s: number
          phase_durations?: Json | null
          recording_id: string
          unknown_s: number
          valid_points: number
          warning_points: number
        }
        Update: {
          algorithm_version?: number
          average_moving_cmps?: number | null
          average_observed_cmps?: number | null
          computed_at?: string
          coverage_ratio?: number
          distance_m?: number
          dropped_points?: number
          filtered_max_speed_cmps?: number | null
          gap_count?: number
          inactive_s?: number
          moving_s?: number
          observed_s?: number
          phase_durations?: Json | null
          recording_id?: string
          unknown_s?: number
          valid_points?: number
          warning_points?: number
        }
        Relationships: [
          {
            foreignKeyName: "recording_summaries_recording_id_fkey"
            columns: ["recording_id"]
            isOneToOne: false
            referencedRelation: "recordings"
            referencedColumns: ["id"]
          },
        ]
      }
      recordings: {
        Row: {
          boot_sequence: number
          clock_quality: string
          collar_id: string
          created_at: string
          ended_at: string | null
          firmware_version: string
          first_point_sequence: number | null
          id: string
          last_point_sequence: number | null
          max_lat_e7: number | null
          max_lon_e7: number | null
          min_lat_e7: number | null
          min_lon_e7: number | null
          point_count: number
          started_at: string | null
          state: string
          telemetry_schema: number
          timezone_at_start: string
          updated_at: string
        }
        Insert: {
          boot_sequence: number
          clock_quality: string
          collar_id: string
          created_at?: string
          ended_at?: string | null
          firmware_version: string
          first_point_sequence?: number | null
          id?: string
          last_point_sequence?: number | null
          max_lat_e7?: number | null
          max_lon_e7?: number | null
          min_lat_e7?: number | null
          min_lon_e7?: number | null
          point_count?: number
          started_at?: string | null
          state?: string
          telemetry_schema: number
          timezone_at_start: string
          updated_at?: string
        }
        Update: {
          boot_sequence?: number
          clock_quality?: string
          collar_id?: string
          created_at?: string
          ended_at?: string | null
          firmware_version?: string
          first_point_sequence?: number | null
          id?: string
          last_point_sequence?: number | null
          max_lat_e7?: number | null
          max_lon_e7?: number | null
          min_lat_e7?: number | null
          min_lon_e7?: number | null
          point_count?: number
          started_at?: string | null
          state?: string
          telemetry_schema?: number
          timezone_at_start?: string
          updated_at?: string
        }
        Relationships: [
          {
            foreignKeyName: "recordings_collar_id_fkey"
            columns: ["collar_id"]
            isOneToOne: false
            referencedRelation: "collars"
            referencedColumns: ["id"]
          },
        ]
      }
      telemetry_points: {
        Row: {
          boot_sequence: number
          chunk_sequence: number
          collar_id: string
          firmware_version: string
          flags: number
          lat_e7: number | null
          lon_e7: number | null
          point_sequence: number
          position: unknown
          received_at: string
          recorded_at: string | null
          reported_speed_cmps: number | null
          satellites: number | null
          telemetry_schema: number
          time_quality: string
        }
        Insert: {
          boot_sequence: number
          chunk_sequence: number
          collar_id: string
          firmware_version: string
          flags: number
          lat_e7?: number | null
          lon_e7?: number | null
          point_sequence: number
          position?: unknown
          received_at?: string
          recorded_at?: string | null
          reported_speed_cmps?: number | null
          satellites?: number | null
          telemetry_schema: number
          time_quality: string
        }
        Update: {
          boot_sequence?: number
          chunk_sequence?: number
          collar_id?: string
          firmware_version?: string
          flags?: number
          lat_e7?: number | null
          lon_e7?: number | null
          point_sequence?: number
          position?: unknown
          received_at?: string
          recorded_at?: string | null
          reported_speed_cmps?: number | null
          satellites?: number | null
          telemetry_schema?: number
          time_quality?: string
        }
        Relationships: [
          {
            foreignKeyName: "telemetry_points_collar_id_fkey"
            columns: ["collar_id"]
            isOneToOne: false
            referencedRelation: "collars"
            referencedColumns: ["id"]
          },
        ]
      }
    }
    Views: {
      [_ in never]: never
    }
    Functions: {
      consume_device_claim_gateway_v1: {
        Args: {
          p_capabilities: Json
          p_code_digest: string
          p_credential_id: string
          p_device: Json
          p_device_attempt_key: string
          p_device_public_id: string
          p_request_id: string
          p_request_sha256: string
          p_secret_digest: string
          p_source_attempt_key: string
        }
        Returns: Json
      }
      consume_device_claim_v1: {
        Args: {
          p_capabilities: Json
          p_code_digest: string
          p_credential_id: string
          p_device: Json
          p_device_public_id: string
          p_request_id: string
          p_request_sha256: string
          p_secret_digest: string
        }
        Returns: Json
      }
      create_dog_v1: {
        Args: { p_name: string; p_timezone?: string }
        Returns: string
      }
      device_revoke_v1: {
        Args: {
          p_credential_id: string
          p_device_id: string
          p_reason: string
          p_request_id: string
          p_request_sha256: string
          p_secret_digest: string
        }
        Returns: Json
      }
      device_sync_gateway_v1: {
        Args: {
          p_credential_id: string
          p_request: Json
          p_request_id: string
          p_request_sha256: string
          p_secret_digest: string
        }
        Returns: Json
      }
      device_sync_v1: {
        Args: {
          p_credential_id: string
          p_request: Json
          p_request_id: string
          p_request_sha256: string
          p_secret_digest: string
        }
        Returns: Json
      }
      get_deletion_job_v1: { Args: { p_job_id: string }; Returns: Json }
      issue_device_claim_v1: {
        Args: {
          p_code_digest: string
          p_dog_id: string
          p_expires_at: string
          p_max_attempts?: number
          p_requested_by: string
        }
        Returns: string
      }
      mutate_config_resource_v1: {
        Args: {
          p_base_server_version: number
          p_body: Json
          p_body_sha256: string
          p_collar_id: string
          p_mutation_id: string
          p_resource_key: string
          p_resource_schema: number
        }
        Returns: Json
      }
      request_dog_deletion_v1: {
        Args: {
          p_confirmation_version: string
          p_dog_id: string
          p_request_id: string
        }
        Returns: Json
      }
      revoke_collar_v1: { Args: { p_collar_id: string }; Returns: boolean }
    }
    Enums: {
      [_ in never]: never
    }
    CompositeTypes: {
      [_ in never]: never
    }
  }
}

type DatabaseWithoutInternals = Omit<Database, "__InternalSupabase">

type DefaultSchema = DatabaseWithoutInternals[Extract<keyof Database, "public">]

export type Tables<
  DefaultSchemaTableNameOrOptions extends
    | keyof (DefaultSchema["Tables"] & DefaultSchema["Views"])
    | { schema: keyof DatabaseWithoutInternals },
  TableName extends DefaultSchemaTableNameOrOptions extends {
    schema: keyof DatabaseWithoutInternals
  }
    ? keyof (DatabaseWithoutInternals[DefaultSchemaTableNameOrOptions["schema"]]["Tables"] &
        DatabaseWithoutInternals[DefaultSchemaTableNameOrOptions["schema"]]["Views"])
    : never = never,
> = DefaultSchemaTableNameOrOptions extends {
  schema: keyof DatabaseWithoutInternals
}
  ? (DatabaseWithoutInternals[DefaultSchemaTableNameOrOptions["schema"]]["Tables"] &
      DatabaseWithoutInternals[DefaultSchemaTableNameOrOptions["schema"]]["Views"])[TableName] extends {
      Row: infer R
    }
    ? R
    : never
  : DefaultSchemaTableNameOrOptions extends keyof (DefaultSchema["Tables"] &
        DefaultSchema["Views"])
    ? (DefaultSchema["Tables"] &
        DefaultSchema["Views"])[DefaultSchemaTableNameOrOptions] extends {
        Row: infer R
      }
      ? R
      : never
    : never

export type TablesInsert<
  DefaultSchemaTableNameOrOptions extends
    | keyof DefaultSchema["Tables"]
    | { schema: keyof DatabaseWithoutInternals },
  TableName extends DefaultSchemaTableNameOrOptions extends {
    schema: keyof DatabaseWithoutInternals
  }
    ? keyof DatabaseWithoutInternals[DefaultSchemaTableNameOrOptions["schema"]]["Tables"]
    : never = never,
> = DefaultSchemaTableNameOrOptions extends {
  schema: keyof DatabaseWithoutInternals
}
  ? DatabaseWithoutInternals[DefaultSchemaTableNameOrOptions["schema"]]["Tables"][TableName] extends {
      Insert: infer I
    }
    ? I
    : never
  : DefaultSchemaTableNameOrOptions extends keyof DefaultSchema["Tables"]
    ? DefaultSchema["Tables"][DefaultSchemaTableNameOrOptions] extends {
        Insert: infer I
      }
      ? I
      : never
    : never

export type TablesUpdate<
  DefaultSchemaTableNameOrOptions extends
    | keyof DefaultSchema["Tables"]
    | { schema: keyof DatabaseWithoutInternals },
  TableName extends DefaultSchemaTableNameOrOptions extends {
    schema: keyof DatabaseWithoutInternals
  }
    ? keyof DatabaseWithoutInternals[DefaultSchemaTableNameOrOptions["schema"]]["Tables"]
    : never = never,
> = DefaultSchemaTableNameOrOptions extends {
  schema: keyof DatabaseWithoutInternals
}
  ? DatabaseWithoutInternals[DefaultSchemaTableNameOrOptions["schema"]]["Tables"][TableName] extends {
      Update: infer U
    }
    ? U
    : never
  : DefaultSchemaTableNameOrOptions extends keyof DefaultSchema["Tables"]
    ? DefaultSchema["Tables"][DefaultSchemaTableNameOrOptions] extends {
        Update: infer U
      }
      ? U
      : never
    : never

export type Enums<
  DefaultSchemaEnumNameOrOptions extends
    | keyof DefaultSchema["Enums"]
    | { schema: keyof DatabaseWithoutInternals },
  EnumName extends DefaultSchemaEnumNameOrOptions extends {
    schema: keyof DatabaseWithoutInternals
  }
    ? keyof DatabaseWithoutInternals[DefaultSchemaEnumNameOrOptions["schema"]]["Enums"]
    : never = never,
> = DefaultSchemaEnumNameOrOptions extends {
  schema: keyof DatabaseWithoutInternals
}
  ? DatabaseWithoutInternals[DefaultSchemaEnumNameOrOptions["schema"]]["Enums"][EnumName]
  : DefaultSchemaEnumNameOrOptions extends keyof DefaultSchema["Enums"]
    ? DefaultSchema["Enums"][DefaultSchemaEnumNameOrOptions]
    : never

export type CompositeTypes<
  PublicCompositeTypeNameOrOptions extends
    | keyof DefaultSchema["CompositeTypes"]
    | { schema: keyof DatabaseWithoutInternals },
  CompositeTypeName extends PublicCompositeTypeNameOrOptions extends {
    schema: keyof DatabaseWithoutInternals
  }
    ? keyof DatabaseWithoutInternals[PublicCompositeTypeNameOrOptions["schema"]]["CompositeTypes"]
    : never = never,
> = PublicCompositeTypeNameOrOptions extends {
  schema: keyof DatabaseWithoutInternals
}
  ? DatabaseWithoutInternals[PublicCompositeTypeNameOrOptions["schema"]]["CompositeTypes"][CompositeTypeName]
  : PublicCompositeTypeNameOrOptions extends keyof DefaultSchema["CompositeTypes"]
    ? DefaultSchema["CompositeTypes"][PublicCompositeTypeNameOrOptions]
    : never

export const Constants = {
  api: {
    Enums: {},
  },
} as const
